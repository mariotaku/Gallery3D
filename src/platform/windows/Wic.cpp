#include "platform/windows/Wic.h"

#include <climits>

namespace Wic {

namespace {

// COM and a factory for the calling thread, for as long as the thread runs.
// Decode threads come and go, so each starts COM the first time it decodes.
struct ThreadImaging {
    ThreadImaging() {
        const HRESULT started = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        // A thread already in a single-threaded apartment, such as the one the
        // window runs on, can still create and use WIC objects; it just is not
        // ours to uninitialize.
        mStarted = SUCCEEDED(started);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    }

    ~ThreadImaging() {
        factory.reset();
        if (mStarted) {
            CoUninitialize();
        }
    }

    Ptr<IWICImagingFactory> factory;

  private:
    bool mStarted = false;
};

}  // namespace

IWICImagingFactory *factory() {
    static thread_local ThreadImaging imaging;
    return imaging.factory.get();
}

Ptr<IWICBitmapDecoder> decoderFor(const void *bytes, size_t size) {
    Ptr<IWICBitmapDecoder> decoder;
    IWICImagingFactory *imaging = factory();
    if (imaging == nullptr || bytes == nullptr || size == 0 || size > MAXDWORD) {
        return decoder;
    }
    Ptr<IWICStream> stream;
    if (FAILED(imaging->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromMemory((BYTE *)bytes, (DWORD)size))) {
        return decoder;
    }
    // The decoder takes its own reference to the stream.
    if (FAILED(imaging->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                                decoder.put()))) {
        decoder.reset();
    }
    return decoder;
}

Bitmap copy(IWICBitmapSource *source, const WICRect *rect) {
    IWICImagingFactory *imaging = factory();
    if (imaging == nullptr || source == nullptr) {
        return Bitmap();
    }
    // BGRA, which WIC converts to from JPEG's BGR far faster than to RGBA, with
    // red and blue swapped afterwards.
    Ptr<IWICFormatConverter> converter;
    if (FAILED(imaging->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return Bitmap();
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height))) {
        return Bitmap();
    }
    if (rect != nullptr) {
        width = (UINT)rect->Width;
        height = (UINT)rect->Height;
    }
    // CopyPixels counts its buffer in a UINT.
    if ((unsigned long long)width * height * 4 > UINT_MAX) {
        return Bitmap();
    }
    Bitmap bitmap((int)width, (int)height);
    if (!bitmap.valid() ||
        FAILED(converter->CopyPixels(rect, width * 4, width * height * 4, bitmap.pixels()))) {
        return Bitmap();
    }
    uint8_t *pixel = bitmap.pixels();
    const uint8_t *end = pixel + (size_t)width * height * 4;
    for (; pixel < end; pixel += 4) {
        const uint8_t blue = pixel[0];
        pixel[0] = pixel[2];
        pixel[2] = blue;
    }
    return bitmap;
}

bool hasAlpha(IWICBitmapSource *source) {
    IWICImagingFactory *imaging = factory();
    WICPixelFormatGUID format;
    if (imaging == nullptr || source == nullptr || FAILED(source->GetPixelFormat(&format))) {
        return false;
    }
    Ptr<IWICComponentInfo> info;
    Ptr<IWICPixelFormatInfo2> pixelInfo;
    BOOL transparent = FALSE;
    if (FAILED(imaging->CreateComponentInfo(format, info.put())) ||
        FAILED(info->QueryInterface(IID_PPV_ARGS(pixelInfo.put()))) ||
        FAILED(pixelInfo->SupportsTransparency(&transparent))) {
        return false;
    }
    return transparent != FALSE;
}

}  // namespace Wic
