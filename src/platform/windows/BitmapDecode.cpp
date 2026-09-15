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

// An image stored beside the frame, if it covers wanted on its long edge in the
// frame's shape.
Wic::Ptr<IWICBitmapSource> embeddedCovering(HRESULT fetched, Wic::Ptr<IWICBitmapSource> image, UINT wanted,
                                            UINT frameWidth, UINT frameHeight) {
    UINT width = 0;
    UINT height = 0;
    if (FAILED(fetched) || !image || FAILED(image->GetSize(&width, &height)) || longEdge(width, height) < wanted ||
        !Wic::sameShape(width, height, frameWidth, frameHeight)) {
        image.reset();
    }
    return image;
}

// A four channel picture with no CMYK profile, converted without colour
// management, the way every platform here converts one. WIC would otherwise run
// it through a system CMYK profile, and the colours would depend on Windows.
Bitmap fromCmyk(IWICBitmapSource *source, UINT targetWidth, UINT targetHeight) {
    const Bitmap bitmap = Wic::copyInks(source, nullptr);
    return bitmap.valid() ? bitmap.scaled((int)targetWidth, (int)targetHeight) : Bitmap();
}

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

    // The size asked for, in the frame's shape and never larger than it.
    const Bitmap::Size fitted = Bitmap::fitWithin((int)width, (int)height, maxEdge);
    const UINT targetWidth = (UINT)fitted.width;
    const UINT targetHeight = (UINT)fitted.height;
    const UINT wanted = longEdge(targetWidth, targetHeight);
    const bool reducing = wanted < longEdge(width, height);

    // What to decode from. An embedded thumbnail that covers the request costs
    // a tenth of reducing a HEIF. A codec that cannot reduce while decoding,
    // which is RAW's, is better served by the full-size JPEG preview the camera
    // stored beside the raw data. Everything else decodes the frame, which a
    // codec like JPEG's reduces as it goes.
    Wic::Ptr<IWICBitmapSource> source;
    if (reducing) {
        Wic::Ptr<IWICBitmapSource> thumbnail;
        const HRESULT fetched = frame->GetThumbnail(thumbnail.put());
        source = embeddedCovering(fetched, std::move(thumbnail), wanted, width, height);
    }
    if (!source) {
        Wic::Ptr<IWICBitmapSourceTransform> transform;
        if (FAILED(frame->QueryInterface(IID_PPV_ARGS(transform.put())))) {
            Wic::Ptr<IWICBitmapSource> preview;
            const HRESULT fetched = decoder->GetPreview(preview.put());
            source = embeddedCovering(fetched, std::move(preview), wanted, width, height);
        }
    }
    bool fromFrame = false;
    if (!source) {
        if (FAILED(frame->QueryInterface(IID_PPV_ARGS(source.put())))) {
            return Bitmap();
        }
        fromFrame = true;
    }

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    source->GetSize(&sourceWidth, &sourceHeight);
    // A format with no alpha channel decodes fully opaque, which saves the
    // upload a pass over the pixels to find that out.
    const bool alpha = Wic::hasAlpha(source.get());
    // A JPEG frame reduces inside its codec by a half, a quarter or an eighth,
    // the smallest that still covers the size asked for, and is scaled the rest
    // of the way here, as libjpeg's decode is on the other platforms.
    if (reducing && fromFrame && !alpha) {
        for (UINT sample = 8; sample >= 2; sample /= 2) {
            if (longEdge((width + sample - 1) / sample, (height + sample - 1) / sample) < wanted) {
                continue;
            }
            const Wic::Ptr<IWICBitmap> reduced = Wic::reducedByCodec(frame.get(), sample, nullptr);
            if (!reduced) {
                continue;
            }
            Bitmap bitmap = (!profile && Wic::isCmyk(reduced.get()))
                                ? Wic::copyInks(reduced.get(), nullptr)
                                : Wic::copy(Wic::inSrgb(reduced.get(), profile.get()).get(), nullptr);
            if (!bitmap.valid()) {
                break;
            }
            bitmap.markOpaque();
            return bitmap.scaledCovering((int)targetWidth, (int)targetHeight, (double)width / sample,
                                         (double)height / sample);
        }
    }
    if (!profile && Wic::isCmyk(source.get())) {
        return fromCmyk(source.get(), targetWidth, targetHeight);
    }
    if (sourceWidth == targetWidth && sourceHeight == targetHeight) {
        Bitmap bitmap = Wic::copy(Wic::inSrgb(source.get(), profile.get()).get(), nullptr);
        if (!alpha) {
            bitmap.markOpaque();
        } else {
            // WIC hands some formats out with alpha whether or not the file
            // uses it, WebP among them.
            bitmap.markOpaqueUnlessTransparent();
        }
        return bitmap;
    }

    // With alpha, converted to sRGB while the colour is still straight, which
    // is what a profile describes, then scaled once premultiplied, so a
    // transparent pixel lends no colour to its neighbours.
    Wic::Ptr<IWICBitmapScaler> scaler;
    if (alpha) {
        const Wic::Ptr<IWICBitmapSource> straight = Wic::inSrgb(source.get(), profile.get());
        Wic::Ptr<IWICFormatConverter> premultiplied;
        if (!straight || FAILED(imaging->CreateFormatConverter(premultiplied.put())) ||
            FAILED(premultiplied->Initialize(straight.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                             nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
            FAILED(imaging->CreateBitmapScaler(scaler.put())) ||
            FAILED(scaler->Initialize(premultiplied.get(), targetWidth, targetHeight,
                                      WICBitmapInterpolationModeFant))) {
            return Bitmap();
        }
        Bitmap bitmap = Wic::copy(scaler.get(), nullptr);
        bitmap.markOpaqueUnlessTransparent();
        return bitmap;
    }
    // Without, the scaler sits straight on the source, which is what lets WIC
    // hand the reduction to the codec, and only the pixels kept are converted.
    if (FAILED(imaging->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(source.get(), targetWidth, targetHeight, WICBitmapInterpolationModeFant))) {
        return Bitmap();
    }
    Bitmap bitmap = Wic::copy(Wic::inSrgb(scaler.get(), profile.get()).get(), nullptr);
    bitmap.markOpaque();
    return bitmap;
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
