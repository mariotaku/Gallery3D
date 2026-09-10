#include "Bitmap.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// Multiplies each colour channel by its alpha. The renderer blends with
// GL_ONE / GL_ONE_MINUS_SRC_ALPHA, which expects premultiplied source pixels.
void premultiply(uint8_t *pixels, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        uint8_t *p = pixels + i * 4;
        unsigned a = p[3];
        if (a == 255) {
            continue;
        }
        p[0] = (uint8_t)((p[0] * a + 127) / 255);
        p[1] = (uint8_t)((p[1] * a + 127) / 255);
        p[2] = (uint8_t)((p[2] * a + 127) / 255);
    }
}

Bitmap fromSurface(SDL_Surface *surface) {
    Bitmap result;
    if (!surface) {
        return result;
    }
    SDL_Surface *rgba = surface;
    bool owned = false;
    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        owned = true;
    }
    if (rgba) {
        result = Bitmap(rgba->w, rgba->h);
        const uint8_t *src = (const uint8_t *)rgba->pixels;
        uint8_t *dst = result.pixels();
        for (int y = 0; y < rgba->h; ++y) {
            std::memcpy(dst + (size_t)y * (size_t)rgba->w * 4, src + (size_t)y * (size_t)rgba->pitch,
                        (size_t)rgba->w * 4);
        }
        premultiply(dst, (size_t)rgba->w * (size_t)rgba->h);
    }
    if (owned && rgba) {
        SDL_DestroySurface(rgba);
    }
    return result;
}

SDL_Surface *toSurface(const Bitmap &bitmap) {
    if (!bitmap.valid()) {
        return nullptr;
    }
    return SDL_CreateSurfaceFrom(bitmap.width(), bitmap.height(), SDL_PIXELFORMAT_RGBA32,
                                 (void *)bitmap.pixels(), bitmap.width() * 4);
}

}  // namespace

Bitmap::Bitmap(int width, int height) : mWidth(width), mHeight(height) {
    if (width > 0 && height > 0) {
        mPixels.assign((size_t)width * (size_t)height * 4, 0);
    } else {
        mWidth = 0;
        mHeight = 0;
    }
}

Bitmap Bitmap::load(const std::string &path, int maxEdge) {
    SDL_Surface *surface = IMG_Load(path.c_str());
    if (!surface) {
        return Bitmap();
    }
    Bitmap decoded = fromSurface(surface);
    SDL_DestroySurface(surface);
    if (!decoded.valid() || maxEdge <= 0) {
        return decoded;
    }
    int longest = std::max(decoded.mWidth, decoded.mHeight);
    if (longest <= maxEdge) {
        return decoded;
    }
    float ratio = (float)maxEdge / (float)longest;
    int newWidth = std::max(1, (int)(decoded.mWidth * ratio));
    int newHeight = std::max(1, (int)(decoded.mHeight * ratio));
    return decoded.scaled(newWidth, newHeight);
}

Bitmap Bitmap::scaled(int newWidth, int newHeight) const {
    if (!valid() || newWidth <= 0 || newHeight <= 0) {
        return Bitmap();
    }
    if (newWidth == mWidth && newHeight == mHeight) {
        return *this;
    }
    SDL_Surface *src = toSurface(*this);
    if (!src) {
        return Bitmap();
    }
    SDL_Surface *dst = SDL_CreateSurface(newWidth, newHeight, SDL_PIXELFORMAT_RGBA32);
    Bitmap result;
    if (dst) {
        SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
        SDL_BlitSurfaceScaled(src, nullptr, dst, nullptr, SDL_SCALEMODE_LINEAR);
        result = Bitmap(newWidth, newHeight);
        const uint8_t *pixels = (const uint8_t *)dst->pixels;
        for (int y = 0; y < newHeight; ++y) {
            std::memcpy(result.pixels() + (size_t)y * (size_t)newWidth * 4,
                        pixels + (size_t)y * (size_t)dst->pitch, (size_t)newWidth * 4);
        }
        SDL_DestroySurface(dst);
    }
    SDL_DestroySurface(src);
    return result;
}

Bitmap Bitmap::paddedTo(int paddedWidth, int paddedHeight) const {
    if (!valid() || paddedWidth < mWidth || paddedHeight < mHeight) {
        return *this;
    }
    Bitmap result(paddedWidth, paddedHeight);
    for (int y = 0; y < mHeight; ++y) {
        std::memcpy(result.pixels() + (size_t)y * (size_t)paddedWidth * 4,
                    mPixels.data() + (size_t)y * (size_t)mWidth * 4, (size_t)mWidth * 4);
    }
    return result;
}

Bitmap Bitmap::coverCropped(int newWidth, int newHeight) const {
    if (!valid() || newWidth <= 0 || newHeight <= 0) {
        return Bitmap();
    }
    float scale = std::max((float)newWidth / (float)mWidth, (float)newHeight / (float)mHeight);
    int scaledWidth = std::max(newWidth, (int)(mWidth * scale + 0.5f));
    int scaledHeight = std::max(newHeight, (int)(mHeight * scale + 0.5f));
    Bitmap scaled = this->scaled(scaledWidth, scaledHeight);
    if (!scaled.valid()) {
        return Bitmap();
    }

    int offsetX = (scaledWidth - newWidth) / 2;
    int offsetY = (scaledHeight - newHeight) / 2;
    Bitmap result(newWidth, newHeight);
    for (int y = 0; y < newHeight; ++y) {
        std::memcpy(result.pixels() + (size_t)y * (size_t)newWidth * 4,
                    scaled.pixels() + ((size_t)(y + offsetY) * (size_t)scaledWidth + (size_t)offsetX) * 4,
                    (size_t)newWidth * 4);
    }
    return result;
}

float Bitmap::readExifRotation(const std::string &path) {
    // Walks the JPEG APP1 segment far enough to find tag 0x0112, the orientation.
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return 0.0f;
    }
    float degrees = 0.0f;
    std::vector<uint8_t> header(65536);
    size_t read = std::fread(header.data(), 1, header.size(), file);
    std::fclose(file);
    if (read < 12 || header[0] != 0xFF || header[1] != 0xD8) {
        return 0.0f;
    }

    size_t pos = 2;
    while (pos + 4 <= read) {
        if (header[pos] != 0xFF) {
            break;
        }
        uint8_t marker = header[pos + 1];
        size_t length = ((size_t)header[pos + 2] << 8) | header[pos + 3];
        if (length < 2 || pos + 2 + length > read) {
            break;
        }
        if (marker == 0xE1 && length >= 16 && std::memcmp(&header[pos + 4], "Exif\0\0", 6) == 0) {
            const uint8_t *tiff = &header[pos + 10];
            size_t tiffLength = length - 8;
            bool bigEndian = tiff[0] == 'M';
            auto read16 = [&](size_t offset) -> unsigned {
                if (offset + 2 > tiffLength) {
                    return 0;
                }
                return bigEndian ? (unsigned)((tiff[offset] << 8) | tiff[offset + 1])
                                 : (unsigned)((tiff[offset + 1] << 8) | tiff[offset]);
            };
            auto read32 = [&](size_t offset) -> unsigned {
                if (offset + 4 > tiffLength) {
                    return 0;
                }
                if (bigEndian) {
                    return ((unsigned)tiff[offset] << 24) | ((unsigned)tiff[offset + 1] << 16) |
                           ((unsigned)tiff[offset + 2] << 8) | (unsigned)tiff[offset + 3];
                }
                return ((unsigned)tiff[offset + 3] << 24) | ((unsigned)tiff[offset + 2] << 16) |
                       ((unsigned)tiff[offset + 1] << 8) | (unsigned)tiff[offset];
            };
            unsigned ifdOffset = read32(4);
            unsigned entryCount = read16(ifdOffset);
            for (unsigned i = 0; i < entryCount; ++i) {
                size_t entry = (size_t)ifdOffset + 2 + (size_t)i * 12;
                if (read16(entry) == 0x0112) {
                    unsigned orientation = read16(entry + 8);
                    switch (orientation) {
                    case 6:
                        degrees = 90.0f;
                        break;
                    case 3:
                        degrees = 180.0f;
                        break;
                    case 8:
                        degrees = 270.0f;
                        break;
                    default:
                        degrees = 0.0f;
                        break;
                    }
                    break;
                }
            }
            break;
        }
        if (marker == 0xDA) {
            break;
        }
        pos += 2 + length;
    }
    return degrees;
}
