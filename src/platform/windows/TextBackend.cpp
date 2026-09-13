// Text through DirectWrite.
//
// The font is whichever one Windows writes its own menus in, and DirectWrite
// reaches past it on its own for anything that font has no glyph for, so
// Japanese, Korean and the rest come out as themselves. Measuring and drawing
// go through the same layout object, so a string never measures one width and
// draws another.
#include "graphics/TextBackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace {

// DirectWrite objects are not promised to be thread safe and the loader threads
// all draw text, so every entry point takes this.
std::mutex sMutex;
IDWriteFactory *sFactory = nullptr;
IDWriteGdiInterop *sGdiInterop = nullptr;
std::wstring sFamily;
bool sReady = false;

// The font Windows labels its own interface with, so the wall reads the way the
// rest of the desktop does.
std::wstring systemUiFamily() {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        if (metrics.lfMessageFont.lfFaceName[0] != L'\0') {
            return metrics.lfMessageFont.lfFaceName;
        }
    }
    return L"Segoe UI";
}

std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring wide((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), &wide[0], needed);
    return wide;
}

// Call with sMutex held. The caller releases both.
bool layoutFor(const std::wstring &wide, float fontSize, bool bold, IDWriteTextFormat **format,
               IDWriteTextLayout **layout) {
    *format = nullptr;
    *layout = nullptr;
    if (!sReady || fontSize <= 0.0f) {
        return false;
    }
    HRESULT hr = sFactory->CreateTextFormat(sFamily.c_str(), nullptr,
                                            bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize,
                                            L"", format);
    if (FAILED(hr) || *format == nullptr) {
        return false;
    }
    // One line, never wrapped: every caller here draws a single run and does
    // its own trimming.
    (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    hr = sFactory->CreateTextLayout(wide.c_str(), (UINT32)wide.length(), *format, 1.0e6f, 1.0e6f, layout);
    if (FAILED(hr) || *layout == nullptr) {
        (*format)->Release();
        *format = nullptr;
        return false;
    }
    return true;
}

}  // namespace

namespace TextBackend {

bool init() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sReady) {
        return true;
    }
    IUnknown *unknown = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &unknown);
    if (FAILED(hr) || unknown == nullptr) {
        SDL_Log("DWriteCreateFactory failed (0x%08lx); text will be blank", (unsigned long)hr);
        return false;
    }
    sFactory = (IDWriteFactory *)unknown;
    hr = sFactory->GetGdiInterop(&sGdiInterop);
    if (FAILED(hr) || sGdiInterop == nullptr) {
        SDL_Log("IDWriteFactory::GetGdiInterop failed (0x%08lx); text will be blank", (unsigned long)hr);
        sFactory->Release();
        sFactory = nullptr;
        return false;
    }
    sFamily = systemUiFamily();
    sReady = true;
    return true;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sGdiInterop) {
        sGdiInterop->Release();
        sGdiInterop = nullptr;
    }
    if (sFactory) {
        sFactory->Release();
        sFactory = nullptr;
    }
    sFamily.clear();
    sReady = false;
}

bool ready() {
    std::lock_guard<std::mutex> lock(sMutex);
    return sReady;
}

bool measure(const std::string &text, float fontSize, bool bold, int *width, int *height) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sReady) {
        return false;
    }
    const std::wstring wide = widen(text);
    IDWriteTextFormat *format = nullptr;
    IDWriteTextLayout *layout = nullptr;
    if (!layoutFor(wide, fontSize, bold, &format, &layout)) {
        return false;
    }
    DWRITE_TEXT_METRICS metrics = {};
    const HRESULT hr = layout->GetMetrics(&metrics);
    layout->Release();
    format->Release();
    if (FAILED(hr)) {
        return false;
    }
    if (width) {
        // Rounded up: a fraction of a pixel still needs a pixel to draw in.
        *width = (int)std::ceil(metrics.widthIncludingTrailingWhitespace);
    }
    if (height) {
        *height = (int)std::ceil(metrics.height);
    }
    return true;
}

Bitmap render(const std::string &text, float fontSize, bool bold) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sReady) {
        return Bitmap();
    }
    const std::wstring wide = widen(text);
    IDWriteTextFormat *format = nullptr;
    IDWriteTextLayout *layout = nullptr;
    if (!layoutFor(wide, fontSize, bold, &format, &layout)) {
        return Bitmap();
    }
    DWRITE_TEXT_METRICS metrics = {};
    if (FAILED(layout->GetMetrics(&metrics))) {
        layout->Release();
        format->Release();
        return Bitmap();
    }
    // Exactly what measure() reports, drawn from the corner. The caller places
    // this against a box it sized from that measurement and reads the baseline
    // off the top edge, so a margin here would drop every line by its width.
    const int width = (int)std::ceil(metrics.widthIncludingTrailingWhitespace);
    const int height = (int)std::ceil(metrics.height);
    if (width <= 0 || height <= 0) {
        layout->Release();
        format->Release();
        return Bitmap();
    }

    IDWriteBitmapRenderTarget *target = nullptr;
    IDWriteRenderingParams *params = nullptr;
    HRESULT hr = sGdiInterop->CreateBitmapRenderTarget(nullptr, (UINT32)width, (UINT32)height, &target);
    if (SUCCEEDED(hr)) {
        hr = sFactory->CreateCustomRenderingParams(1.0f, 0.0f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                                                   DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC, &params);
    }
    if (FAILED(hr) || target == nullptr || params == nullptr) {
        if (params) {
            params->Release();
        }
        if (target) {
            target->Release();
        }
        layout->Release();
        format->Release();
        return Bitmap();
    }

    // One pixel to one device independent pixel. The target would otherwise
    // take the display's own scaling and draw larger than GetMetrics just
    // reported, which is measuring in one unit and drawing in another. The
    // caller has already scaled the size it asked for.
    target->SetPixelsPerDip(1.0f);

    // The target starts out black and the glyphs go down in white, so what
    // comes back out is the coverage the caller wants to tint.
    HDC dc = target->GetMemoryDC();
    const HBITMAP dib = (HBITMAP)GetCurrentObject(dc, OBJ_BITMAP);
    BITMAP info = {};
    GetObject(dib, sizeof(info), &info);
    if (info.bmBits != nullptr) {
        std::memset(info.bmBits, 0, (size_t)std::abs(info.bmHeight) * (size_t)info.bmWidthBytes);
    }

    // DrawGlyphRun through the target does the fallback: a run the chosen face
    // cannot draw arrives here already mapped to a face that can.
    struct Renderer : IDWriteTextRenderer {
        IDWriteBitmapRenderTarget *target;
        IDWriteRenderingParams *params;
        ULONG refs = 1;

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
            if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) ||
                riid == __uuidof(IDWriteTextRenderer)) {
                *out = this;
                ++refs;
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++refs;
        }
        ULONG STDMETHODCALLTYPE Release() override {
            return --refs;
        }
        HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *, BOOL *disabled) override {
            *disabled = FALSE;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *, DWRITE_MATRIX *transform) override {
            return target->GetCurrentTransform(transform);
        }
        HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, FLOAT *pixelsPerDip) override {
            *pixelsPerDip = target->GetPixelsPerDip();
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode,
                                               const DWRITE_GLYPH_RUN *run,
                                               const DWRITE_GLYPH_RUN_DESCRIPTION *, IUnknown *) override {
            RECT dirty = {};
            return target->DrawGlyphRun(x, y, mode, run, params, RGB(255, 255, 255), &dirty);
        }
        HRESULT STDMETHODCALLTYPE DrawUnderline(void *, FLOAT, FLOAT, const DWRITE_UNDERLINE *,
                                                IUnknown *) override {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH *,
                                                    IUnknown *) override {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DrawInlineObject(void *, FLOAT, FLOAT, IDWriteInlineObject *, BOOL, BOOL,
                                                   IUnknown *) override {
            return S_OK;
        }
    };
    Renderer renderer;
    renderer.target = target;
    renderer.params = params;
    hr = layout->Draw(nullptr, &renderer, 0.0f, 0.0f);

    Bitmap glyphs;
    if (SUCCEEDED(hr) && info.bmBits != nullptr && info.bmBitsPixel == 32) {
        glyphs = Bitmap(width, height);
        const int rows = std::min(height, (int)std::abs(info.bmHeight));
        const int columns = std::min(width, (int)info.bmWidth);
        for (int y = 0; y < rows; ++y) {
            const uint8_t *source = (const uint8_t *)info.bmBits + (size_t)y * (size_t)info.bmWidthBytes;
            uint8_t *out = glyphs.pixels() + (size_t)y * (size_t)width * 4;
            for (int x = 0; x < columns; ++x) {
                const uint8_t *bgr = source + (size_t)x * 4;
                // White on black, so any channel is the coverage. Take the
                // strongest, which keeps a cleartype fringe from thinning a
                // stem that the caller is about to tint one colour anyway.
                const uint8_t coverage = std::max(bgr[0], std::max(bgr[1], bgr[2]));
                uint8_t *pixel = out + (size_t)x * 4;
                pixel[0] = 255;
                pixel[1] = 255;
                pixel[2] = 255;
                pixel[3] = coverage;
            }
        }
    }

    params->Release();
    target->Release();
    layout->Release();
    format->Release();
    return glyphs;
}

}  // namespace TextBackend
