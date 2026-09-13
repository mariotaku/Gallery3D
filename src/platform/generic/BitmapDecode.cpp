// Image decoding and PNG writing through SDL_image, with a reduced decode
// inside the codec first wherever SubsampledDecode has one.
#include "graphics/Bitmap.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

#include "graphics/SubsampledDecode.h"

namespace {

// The surface's pixels as a premultiplied Bitmap. Takes ownership of the
// surface.
Bitmap fromSurface(SDL_Surface *surface) {
    if (surface == nullptr) {
        return Bitmap();
    }
    SDL_Surface *rgba =
        (surface->format == SDL_PIXELFORMAT_RGBA32) ? surface : SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    Bitmap result;
    if (rgba != nullptr) {
        if (rgba->pitch == rgba->w * 4) {
            result = Bitmap::fromStraightRGBA((const uint8_t *)rgba->pixels, rgba->w, rgba->h);
        } else {
            std::vector<uint8_t> rows((size_t)rgba->w * (size_t)rgba->h * 4);
            for (int y = 0; y < rgba->h; ++y) {
                std::memcpy(rows.data() + (size_t)y * (size_t)rgba->w * 4,
                            (const uint8_t *)rgba->pixels + (size_t)y * (size_t)rgba->pitch, (size_t)rgba->w * 4);
            }
            result = Bitmap::fromStraightRGBA(rows.data(), rgba->w, rgba->h);
        }
        if (rgba != surface) {
            SDL_DestroySurface(rgba);
        }
    }
    SDL_DestroySurface(surface);
    return result;
}

// Brings a decoded image down to maxEdge on its long edge, keeping its shape.
Bitmap trimToMaxEdge(Bitmap decoded, int maxEdge) {
    if (!decoded.valid() || maxEdge <= 0) {
        return decoded;
    }
    int longest = std::max(decoded.width(), decoded.height());
    if (longest <= maxEdge) {
        return decoded;
    }
    float ratio = (float)maxEdge / (float)longest;
    int newWidth = std::max(1, (int)(decoded.width() * ratio));
    int newHeight = std::max(1, (int)(decoded.height() * ratio));
    return decoded.scaled(newWidth, newHeight);
}

}  // namespace

Bitmap Bitmap::load(const std::string &path, int maxEdge) {
    return trimToMaxEdge(fromSurface(IMG_Load(path.c_str())), maxEdge);
}

Bitmap Bitmap::loadFromMemory(const void *bytes, size_t size, int maxEdge) {
    if (bytes == nullptr || size == 0) {
        return Bitmap();
    }
    if (maxEdge > 0) {
        // Reduce inside the decoder where it is close to free, rather than
        // building the full size image only to throw most of it away. What
        // comes back can land a little under maxEdge, or over it, in which
        // case the tail below trims it to exactly that.
        Bitmap reduced = SubsampledDecode::decode(bytes, size, maxEdge);
        if (reduced.valid()) {
            return trimToMaxEdge(std::move(reduced), maxEdge);
        }
    }
    SDL_IOStream *stream = SDL_IOFromConstMem(bytes, size);
    if (stream == nullptr) {
        return Bitmap();
    }
    // IMG_Load_IO closes the stream for us, including on failure.
    return trimToMaxEdge(fromSurface(IMG_Load_IO(stream, true)), maxEdge);
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
    SDL_Surface *surface = SDL_CreateSurfaceFrom(mWidth, mHeight, SDL_PIXELFORMAT_RGBA32, straight.data(), mWidth * 4);
    if (surface == nullptr) {
        return false;
    }
    const bool saved = IMG_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    return saved;
}
