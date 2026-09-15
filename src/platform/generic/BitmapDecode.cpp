// Image decoding and PNG writing through SDL_image, with a reduced decode
// inside the codec first wherever SubsampledDecode has one.
#include "graphics/Bitmap.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include "graphics/SubsampledDecode.h"

namespace {

// The surface's pixels as a premultiplied Bitmap, converted straight into the
// bitmap in one pass. A picture without alpha is premultiplied as it is, so
// only one with alpha takes a second pass, and the other is marked opaque.
// Takes ownership of the surface.
Bitmap fromSurface(SDL_Surface *surface) {
    if (surface == nullptr) {
        return Bitmap();
    }
    // A palette or a colour key hangs off the surface, where SDL_ConvertPixels
    // cannot see it, so such a picture is converted as a surface first.
    SDL_Surface *source = (SDL_ISPIXELFORMAT_INDEXED(surface->format) || SDL_SurfaceHasColorKey(surface))
                              ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32)
                              : surface;
    Bitmap result;
    if (source != nullptr) {
        Bitmap bitmap(source->w, source->h);
        if (bitmap.valid() && SDL_ConvertPixels(source->w, source->h, source->format, source->pixels, source->pitch,
                                                SDL_PIXELFORMAT_RGBA32, bitmap.pixels(), source->w * 4)) {
            if (SDL_ISPIXELFORMAT_ALPHA(source->format)) {
                bitmap.premultiply();
                bitmap.markOpaqueUnlessTransparent();
            } else {
                bitmap.markOpaque();
            }
            result = std::move(bitmap);
        }
        if (source != surface) {
            SDL_DestroySurface(source);
        }
    }
    SDL_DestroySurface(surface);
    return result;
}

// Brings a whole decoded image to the size the rule gives for maxEdge.
Bitmap fitToMaxEdge(Bitmap decoded, int maxEdge) {
    if (!decoded.valid()) {
        return decoded;
    }
    const Bitmap::Size size = Bitmap::fitWithin(decoded.width(), decoded.height(), maxEdge);
    return decoded.scaled(size.width, size.height);
}

}  // namespace

Bitmap Bitmap::load(const std::string &path, int maxEdge) {
    // Through the bytes, so a JPEG asked for smaller than it is reduces inside
    // libjpeg rather than decoding whole and scaling after.
    std::vector<uint8_t> bytes;
    if (!readFile(path, &bytes)) {
        return Bitmap();
    }
    return loadFromMemory(bytes.data(), bytes.size(), maxEdge);
}

Bitmap Bitmap::loadFromMemory(const void *bytes, size_t size, int maxEdge) {
    if (bytes == nullptr || size == 0) {
        return Bitmap();
    }
    // The thumbnail a camera stores beside the photo, when a reduced decode
    // asks for no more than it holds and it has the photo's shape. WIC answers
    // from it the same way on Windows.
    if (maxEdge > 0) {
        const ExifInfo exif = readExif(bytes, size);
        if (exif.thumbnailLength > 0 && exif.pixelWidth > 0 && exif.pixelHeight > 0) {
            const Size target = fitWithin(exif.pixelWidth, exif.pixelHeight, maxEdge);
            if (target.width < exif.pixelWidth || target.height < exif.pixelHeight) {
                Bitmap thumbnail =
                    loadFromMemory((const uint8_t *)bytes + exif.thumbnailOffset, exif.thumbnailLength, 0);
                if (thumbnail.valid() &&
                    std::max(thumbnail.width(), thumbnail.height()) >= std::max(target.width, target.height) &&
                    sameShape(thumbnail.width(), thumbnail.height(), exif.pixelWidth, exif.pixelHeight)) {
                    return thumbnail.scaled(target.width, target.height);
                }
            }
        }
    }
    // The platform's own decoder first, where it has one. It reduces inside the
    // codec where that is close to free, rather than building the full size
    // image only to throw most of it away, and on the desktop it converts a
    // JPEG's colour profile at any size. It answers at the size the rule gives,
    // which only it can work out, since it alone knows the original's size.
    Bitmap decoded = SubsampledDecode::decode(bytes, size, maxEdge);
    if (decoded.valid()) {
        return decoded;
    }
    SDL_IOStream *stream = SDL_IOFromConstMem(bytes, size);
    if (stream == nullptr) {
        return Bitmap();
    }
    // IMG_Load_IO closes the stream for us, including on failure.
    return fitToMaxEdge(fromSurface(IMG_Load_IO(stream, true)), maxEdge);
}

PixelOrder Bitmap::decodeOrder() {
    return PixelOrder::RGBA;
}

bool Bitmap::decodesExtension(const std::string &extension) {
    // What SDL_image is built to read on the platforms that take this file.
    std::string lower = extension;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return lower == ".jpg" || lower == ".jpeg" || lower == ".png" || lower == ".bmp" || lower == ".gif" ||
           lower == ".webp" || lower == ".tif" || lower == ".tiff";
}

bool Bitmap::savePng(const std::string &path) const {
    if (!valid()) {
        return false;
    }
    // PNG stores straight alpha, and these pixels are premultiplied.
    std::vector<uint8_t> straight(mPixels);
    for (size_t i = 0; i < straight.size(); i += 4) {
        const unsigned alpha = straight[i + 3];
        if (alpha != 0 && alpha != 255) {
            for (size_t channel = 0; channel < 3; ++channel) {
                straight[i + channel] =
                    (uint8_t)std::min(255u, (straight[i + channel] * 255u + alpha / 2) / alpha);
            }
        }
    }
    const SDL_PixelFormat format = (mOrder == PixelOrder::RGBA) ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_BGRA32;
    SDL_Surface *surface = SDL_CreateSurfaceFrom(mWidth, mHeight, format, straight.data(), mWidth * 4);
    if (surface == nullptr) {
        return false;
    }
    const bool saved = IMG_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    return saved;
}
