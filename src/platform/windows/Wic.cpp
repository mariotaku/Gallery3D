#include "platform/windows/Wic.h"

#include <climits>
#include <cmath>
#include <vector>

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
    // BGRA, which WIC converts to from JPEG's BGR far faster than to RGBA, and
    // which the GPU takes as it is.
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
    Bitmap bitmap((int)width, (int)height, PixelOrder::BGRA);
    if (!bitmap.valid() ||
        FAILED(converter->CopyPixels(rect, width * 4, width * height * 4, bitmap.pixels()))) {
        return Bitmap();
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

Ptr<IWICColorContext> colorProfileOf(IWICBitmapFrameDecode *frame) {
    Ptr<IWICColorContext> profile;
    IWICImagingFactory *imaging = factory();
    UINT count = 0;
    // A codec that keeps no colour information, such as the RAW one, fails
    // here rather than answering none.
    if (imaging == nullptr || frame == nullptr || FAILED(frame->GetColorContexts(0, nullptr, &count)) ||
        count == 0) {
        return profile;
    }
    std::vector<Ptr<IWICColorContext>> contexts(count);
    std::vector<IWICColorContext *> slots(count, nullptr);
    for (UINT i = 0; i < count; ++i) {
        if (FAILED(imaging->CreateColorContext(contexts[i].put()))) {
            return profile;
        }
        slots[i] = contexts[i].get();
    }
    if (FAILED(frame->GetColorContexts(count, slots.data(), &count))) {
        return profile;
    }
    // An ICC profile says the most. EXIF only tells sRGB, 1, from Adobe RGB, 2,
    // and a camera writes 1 beside a profile it also embeds.
    for (Ptr<IWICColorContext> &context : contexts) {
        WICColorContextType type;
        if (context && SUCCEEDED(context->GetType(&type)) && type == WICColorContextProfile) {
            return std::move(context);
        }
    }
    for (Ptr<IWICColorContext> &context : contexts) {
        WICColorContextType type;
        UINT space = 0;
        if (context && SUCCEEDED(context->GetType(&type)) && type == WICColorContextExifColorSpace &&
            SUCCEEDED(context->GetExifColorSpace(&space)) && space == 2) {
            return std::move(context);
        }
    }
    return profile;
}

Ptr<IWICBitmapSource> inSrgb(IWICBitmapSource *source, IWICColorContext *profile) {
    Ptr<IWICBitmapSource> result;
    IWICImagingFactory *imaging = factory();
    Ptr<IWICColorContext> srgb;
    Ptr<IWICColorTransform> transform;
    if (source != nullptr && profile != nullptr && imaging != nullptr &&
        SUCCEEDED(imaging->CreateColorContext(srgb.put())) && SUCCEEDED(srgb->InitializeFromExifColorSpace(1)) &&
        SUCCEEDED(imaging->CreateColorTransformer(transform.put())) &&
        SUCCEEDED(transform->Initialize(source, profile, srgb.get(), GUID_WICPixelFormat32bppBGRA)) &&
        SUCCEEDED(transform->QueryInterface(IID_PPV_ARGS(result.put())))) {
        return result;
    }
    result.reset();
    if (source != nullptr) {
        source->AddRef();
        *result.put() = source;
    }
    return result;
}

Bitmap scaled(const Bitmap &bitmap, int width, int height) {
    IWICImagingFactory *imaging = factory();
    if (imaging == nullptr || !bitmap.valid() || width <= 0 || height <= 0) {
        return Bitmap();
    }
    if (bitmap.width() == width && bitmap.height() == height) {
        return bitmap;
    }
    // PBGRA, which WIC has had since Windows 7. Its RGBA formats arrived with
    // Windows 8, so an RGBA bitmap is reordered first.
    const Bitmap *source = &bitmap;
    Bitmap reordered;
    if (bitmap.order() == PixelOrder::RGBA) {
        reordered = bitmap.inOrder(PixelOrder::BGRA);
        source = &reordered;
    }
    const UINT stride = (UINT)bitmap.width() * 4;
    Ptr<IWICBitmap> pixels;
    Ptr<IWICBitmapScaler> scaler;
    // CreateBitmapFromMemory copies the pixels, so nothing is written back.
    if (FAILED(imaging->CreateBitmapFromMemory((UINT)bitmap.width(), (UINT)bitmap.height(),
                                               GUID_WICPixelFormat32bppPBGRA, stride, stride * (UINT)bitmap.height(),
                                               const_cast<BYTE *>(source->pixels()), pixels.put())) ||
        FAILED(imaging->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(pixels.get(), (UINT)width, (UINT)height, WICBitmapInterpolationModeFant))) {
        return Bitmap();
    }
    Bitmap result = copy(scaler.get(), nullptr);
    if (bitmap.knownOpaque()) {
        result.markOpaque();
    }
    return result;
}

bool sameShape(UINT width, UINT height, UINT frameWidth, UINT frameHeight) {
    if (width == 0 || height == 0 || frameWidth == 0 || frameHeight == 0) {
        return false;
    }
    const double shape = (double)width / (double)height;
    const double frameShape = (double)frameWidth / (double)frameHeight;
    return std::fabs(shape - frameShape) <= 0.02 * frameShape;
}

}  // namespace Wic
