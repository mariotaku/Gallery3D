// Image decoding through WIC, with whichever codecs Windows has installed.
// Beyond the built-in ones those can include HEIF and camera RAW, from their
// Store extensions.
#include "graphics/Bitmap.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_set>

#include "platform/windows/Wic.h"

namespace {

UINT longEdge(UINT width, UINT height) {
    return std::max(width, height);
}

// An image stored beside the frame, if its long edge reaches maxEdge in the
// frame's shape.
Wic::Ptr<IWICBitmapSource> embeddedReaching(HRESULT fetched, Wic::Ptr<IWICBitmapSource> image, int maxEdge,
                                            UINT frameWidth, UINT frameHeight) {
    UINT width = 0;
    UINT height = 0;
    if (FAILED(fetched) || !image || FAILED(image->GetSize(&width, &height)) ||
        longEdge(width, height) < (UINT)maxEdge || !Wic::sameShape(width, height, frameWidth, frameHeight)) {
        image.reset();
    }
    return image;
}

// The source at its own size, in sRGB and premultiplied, and marked opaque when
// its format has no alpha or its pixels use none.
Bitmap wholeOf(IWICBitmapSource *source, IWICColorContext *profile) {
    if (profile == nullptr && Wic::isCmyk(source)) {
        // A four channel picture with no CMYK profile, converted without
        // colour management, the way every platform here converts one. WIC
        // would otherwise run it through a system CMYK profile, and the
        // colours would depend on Windows.
        return Wic::copyInks(source, nullptr);
    }
    // Converted to sRGB while the colour is still straight, which is what a
    // profile describes, and premultiplied by the copy.
    Bitmap bitmap = Wic::copy(Wic::inSrgb(source, profile).get(), nullptr);
    if (!Wic::hasAlpha(source)) {
        bitmap.markOpaque();
    } else {
        // WIC hands some formats out with alpha whether or not the file uses
        // it, WebP among them.
        bitmap.markOpaqueUnlessTransparent();
    }
    return bitmap;
}

// An image stored beside the frame, decoded for maxEdge as a picture of its
// own. Both a thumbnail and a RAW preview are JPEGs.
Bitmap embeddedFor(IWICBitmapSource *image, IWICColorContext *profile, int maxEdge) {
    UINT width = 0;
    UINT height = 0;
    image->GetSize(&width, &height);
    return wholeOf(image, profile).sampledFromWhole(Sampling::Jpeg,
                                                    Bitmap::sampleSizeFor((int)width, (int)height, maxEdge));
}

// Decodes the first frame reduced by the sample maxEdge gives, the way Android's
// decoder reduces the format.
Bitmap decode(IWICBitmapDecoder *decoder, int maxEdge) {
    IWICImagingFactory *imaging = Wic::factory();
    Wic::Ptr<IWICBitmapFrameDecode> frame;
    if (imaging == nullptr || decoder == nullptr || FAILED(decoder->GetFrame(0, frame.put()))) {
        return Bitmap();
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) {
        return Bitmap();
    }
    // What the pixels' colours mean. Every picture leaves here in sRGB, which
    // is what the wall draws in.
    const Wic::Ptr<IWICColorContext> profile = Wic::colorProfileOf(frame.get());
    const int sampleSize = Bitmap::sampleSizeFor((int)width, (int)height, maxEdge);

    // An embedded thumbnail whose long edge reaches maxEdge costs a tenth of
    // reducing a HEIF, and every platform answers from it the same way. A
    // codec that cannot reduce while decoding, which is RAW's, is better served
    // by the JPEG preview the camera stored beside the raw data.
    if (sampleSize > 1) {
        Wic::Ptr<IWICBitmapSource> thumbnail;
        HRESULT fetched = frame->GetThumbnail(thumbnail.put());
        thumbnail = embeddedReaching(fetched, std::move(thumbnail), maxEdge, width, height);
        if (thumbnail) {
            return embeddedFor(thumbnail.get(), profile.get(), maxEdge);
        }
        Wic::Ptr<IWICBitmapSourceTransform> transform;
        if (FAILED(frame->QueryInterface(IID_PPV_ARGS(transform.put())))) {
            Wic::Ptr<IWICBitmapSource> preview;
            fetched = decoder->GetPreview(preview.put());
            preview = embeddedReaching(fetched, std::move(preview), maxEdge, width, height);
            if (preview) {
                return embeddedFor(preview.get(), profile.get(), maxEdge);
            }
        }
    }

    Wic::Ptr<IWICBitmapSource> source;
    if (FAILED(frame->QueryInterface(IID_PPV_ARGS(source.put())))) {
        return Bitmap();
    }
    const Sampling sampling = [decoder]() {
        GUID container;
        if (FAILED(decoder->GetContainerFormat(&container))) {
            return Sampling::Picked;
        }
        return container == GUID_ContainerFormatJpeg   ? Sampling::Jpeg
               : container == GUID_ContainerFormatWebp ? Sampling::Rescaled
                                                       : Sampling::Picked;
    }();

    // A JPEG frame reduces inside its codec by a half, a quarter or an eighth,
    // rounding up, as libjpeg does on the other platforms and Android's decoder
    // does with the same sample. A sample past an eighth is picked from that.
    if (sampling == Sampling::Jpeg && sampleSize > 1) {
        const Wic::Ptr<IWICBitmap> reduced = Wic::reducedByCodec(frame.get(), (UINT)std::min(sampleSize, 8), nullptr);
        if (reduced) {
            const Bitmap bitmap = wholeOf(reduced.get(), profile.get());
            const Bitmap::Size size = Bitmap::sampledSize(Sampling::Jpeg, (int)width, (int)height, sampleSize);
            return bitmap.valid() ? bitmap.picked(size.width, size.height) : Bitmap();
        }
    }
    return wholeOf(source.get(), profile.get()).sampledFromWhole(sampling, sampleSize);
}

std::unordered_set<std::string> installedExtensions() {
    std::unordered_set<std::string> found;
    IWICImagingFactory *imaging = Wic::factory();
    Wic::Ptr<IEnumUnknown> components;
    if (imaging == nullptr ||
        FAILED(imaging->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, components.put()))) {
        return found;
    }
    Wic::Ptr<IUnknown> component;
    ULONG fetched = 0;
    while (components->Next(1, component.put(), &fetched) == S_OK) {
        Wic::Ptr<IWICBitmapDecoderInfo> info;
        UINT length = 0;
        if (FAILED(component->QueryInterface(IID_PPV_ARGS(info.put()))) ||
            FAILED(info->GetFileExtensions(0, nullptr, &length)) || length == 0) {
            continue;
        }
        std::wstring list(length, L'\0');
        if (FAILED(info->GetFileExtensions(length, list.data(), &length))) {
            continue;
        }
        // A comma-separated list, such as ".jpeg,.jpe,.jpg".
        std::string extension;
        for (wchar_t c : list) {
            if (c == L',' || c == L'\0') {
                if (!extension.empty()) {
                    found.insert(extension);
                }
                extension.clear();
            } else if (c < 128) {
                extension.push_back((char)std::tolower((unsigned char)c));
            }
        }
        if (!extension.empty()) {
            found.insert(extension);
        }
    }
    return found;
}

}  // namespace

Bitmap Bitmap::load(const std::string &path, int maxEdge) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, &bytes)) {
        return Bitmap();
    }
    return loadFromMemory(bytes.data(), bytes.size(), maxEdge);
}

Bitmap Bitmap::loadFromMemory(const void *bytes, size_t size, int maxEdge) {
    // WIC hands out what it could read of a PNG or JPEG that ends early, with
    // the rest left empty or grey.
    if (endsEarly(bytes, size)) {
        return Bitmap();
    }
    Wic::Ptr<IWICBitmapDecoder> decoder = Wic::decoderFor(bytes, size);
    return decoder ? decode(decoder.get(), maxEdge) : Bitmap();
}

PixelOrder Bitmap::decodeOrder() {
    return PixelOrder::BGRA;
}

bool Bitmap::decodesExtension(const std::string &extension) {
    // A scan asks once per file. Codecs register when they are installed, so
    // the list is gathered once and holds for the run.
    static const std::unordered_set<std::string> extensions = installedExtensions();
    std::string lower = extension;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return extensions.count(lower) != 0;
}

bool Bitmap::savePng(const std::string &path) const {
    IWICImagingFactory *imaging = Wic::factory();
    if (imaging == nullptr || !valid()) {
        return false;
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    std::wstring widePath((size_t)wideLength, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, widePath.data(), wideLength);

    Wic::Ptr<IWICBitmap> pixels;
    Wic::Ptr<IWICFormatConverter> straight;
    Wic::Ptr<IWICStream> stream;
    Wic::Ptr<IWICBitmapEncoder> encoder;
    Wic::Ptr<IWICBitmapFrameEncode> frame;
    // BGRA, which WIC has had since Windows 7. Its RGBA formats arrived with
    // Windows 8, so an RGBA bitmap is reordered first.
    const Bitmap *source = this;
    Bitmap reordered;
    if (mOrder == PixelOrder::RGBA) {
        reordered = inOrder(PixelOrder::BGRA);
        source = &reordered;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    // PNG stores straight alpha, and these pixels are premultiplied.
    return SUCCEEDED(imaging->CreateBitmapFromMemory((UINT)mWidth, (UINT)mHeight, GUID_WICPixelFormat32bppPBGRA,
                                                     (UINT)mWidth * 4, (UINT)mPixels.size(),
                                                     const_cast<BYTE *>(source->pixels()), pixels.put())) &&
           SUCCEEDED(imaging->CreateFormatConverter(straight.put())) &&
           SUCCEEDED(straight->Initialize(pixels.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                          nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
           SUCCEEDED(imaging->CreateStream(stream.put())) &&
           SUCCEEDED(stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(imaging->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) &&
           SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->SetSize((UINT)mWidth, (UINT)mHeight)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
           SUCCEEDED(frame->WriteSource(straight.get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
           SUCCEEDED(encoder->Commit());
}
