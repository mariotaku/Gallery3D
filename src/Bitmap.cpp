#include "Bitmap.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

// Turns an EXIF "YYYY:MM:DD HH:MM:SS" stamp into milliseconds since the Unix
// epoch. EXIF carries no time zone, so the camera's wall clock is read as local
// time. Cameras also write blank and half filled stamps, hence the range checks.
int64_t parseExifDate(const uint8_t *text, size_t length) {
    if (length < 19) {
        return 0;
    }
    char stamp[20];
    std::memcpy(stamp, text, 19);
    stamp[19] = '\0';
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(stamp, "%4d:%2d:%2d %2d:%2d:%2d", &year, &month, &day, &hour, &minute,
                    &second) != 6) {
        return 0;
    }
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60) {
        return 0;
    }
    std::tm parts = {};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    parts.tm_isdst = -1;  // let the C library work out whether DST was in force
    std::time_t taken = std::mktime(&parts);
    if (taken == (std::time_t)-1) {
        return 0;
    }
    return (int64_t)taken * 1000LL;
}

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

namespace {

// Shared tail of both loaders: take the surface, convert, and bring it down to
// maxEdge if it is over.
Bitmap finishDecode(SDL_Surface *surface, int maxEdge);

}  // namespace

Bitmap Bitmap::load(const std::string &path, int maxEdge) {
    return finishDecode(IMG_Load(path.c_str()), maxEdge);
}

Bitmap Bitmap::loadFromMemory(const void *bytes, size_t size, int maxEdge) {
    if (bytes == nullptr || size == 0) {
        return Bitmap();
    }
    SDL_IOStream *stream = SDL_IOFromConstMem(bytes, size);
    if (stream == nullptr) {
        return Bitmap();
    }
    // IMG_Load_IO closes the stream for us, including on failure.
    return finishDecode(IMG_Load_IO(stream, true), maxEdge);
}

bool Bitmap::readFile(const std::string &path, std::vector<uint8_t> *bytes) {
    if (path.empty() || bytes == nullptr) {
        return false;
    }
    size_t size = 0;
    void *data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        return false;
    }
    const uint8_t *start = (const uint8_t *)data;
    bytes->assign(start, start + size);
    SDL_free(data);
    return !bytes->empty();
}

Bitmap Bitmap::fromStraightRGBA(const uint8_t *pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return Bitmap();
    }
    Bitmap result(width, height);
    if (!result.valid()) {
        return result;
    }
    const size_t count = (size_t)width * (size_t)height;
    std::memcpy(result.pixels(), pixels, count * 4);
    premultiply(result.pixels(), count);
    return result;
}

namespace {

Bitmap finishDecode(SDL_Surface *surface, int maxEdge) {
    if (surface == nullptr) {
        return Bitmap();
    }
    Bitmap decoded = fromSurface(surface);
    SDL_DestroySurface(surface);
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

Bitmap Bitmap::paddedTo(int paddedWidth, int paddedHeight, bool clampEdges) const {
    if (!valid() || paddedWidth < mWidth || paddedHeight < mHeight) {
        return *this;
    }
    Bitmap result(paddedWidth, paddedHeight);
    for (int y = 0; y < mHeight; ++y) {
        uint8_t *row = result.pixels() + (size_t)y * (size_t)paddedWidth * 4;
        std::memcpy(row, mPixels.data() + (size_t)y * (size_t)mWidth * 4, (size_t)mWidth * 4);
        if (clampEdges) {
            // Repeat the last pixel of the row across the rest of it.
            const uint8_t *last = row + (size_t)(mWidth - 1) * 4;
            for (int x = mWidth; x < paddedWidth; ++x) {
                std::memcpy(row + (size_t)x * 4, last, 4);
            }
        }
    }
    if (clampEdges && mHeight > 0) {
        // Then repeat the last row down the rest of the bitmap.
        const uint8_t *last = result.pixels() + (size_t)(mHeight - 1) * (size_t)paddedWidth * 4;
        for (int y = mHeight; y < paddedHeight; ++y) {
            std::memcpy(result.pixels() + (size_t)y * (size_t)paddedWidth * 4, last,
                        (size_t)paddedWidth * 4);
        }
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

Bitmap::ExifInfo Bitmap::readExif(const std::string &path) {
    // Walks the JPEG APP1 segment for the orientation (0x0112, in IFD0), the
    // capture date (0x9003, in the Exif sub-IFD that IFD0's tag 0x8769 points
    // at) and the position (in the GPS sub-IFD that IFD0's tag 0x8825 points
    // at).
    // These are arbitrary user files, so every read is bounds checked and a
    // malformed header just leaves the defaults in place.
    ExifInfo info;
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return info;
    }
    std::vector<uint8_t> header(65536);
    size_t read = std::fread(header.data(), 1, header.size(), file);
    std::fclose(file);
    if (read < 12 || header[0] != 0xFF || header[1] != 0xD8) {
        return info;
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
            // A date does not fit in the entry's four value bytes, so the entry
            // holds an offset to the string instead.
            auto readDate = [&](size_t entry) -> int64_t {
                if (read16(entry + 2) != 2) {  // ASCII
                    return 0;
                }
                size_t count = read32(entry + 4);
                size_t valueOffset = read32(entry + 8);
                if (count > tiffLength || valueOffset > tiffLength - count) {
                    return 0;
                }
                return parseExifDate(tiff + valueOffset, count);
            };

            // A GPS coordinate is three rationals, degrees, minutes and
            // seconds, held at an offset because twenty four bytes do not fit
            // in the entry.
            auto readCoordinate = [&](size_t entry, double *out) -> bool {
                if (read16(entry + 2) != 5 || read32(entry + 4) != 3) {  // three RATIONALs
                    return false;
                }
                size_t valueOffset = read32(entry + 8);
                if (valueOffset > tiffLength || tiffLength - valueOffset < 24) {
                    return false;
                }
                double parts[3];
                for (int part = 0; part < 3; ++part) {
                    unsigned numerator = read32(valueOffset + (size_t)part * 8);
                    unsigned denominator = read32(valueOffset + (size_t)part * 8 + 4);
                    if (denominator == 0) {
                        return false;
                    }
                    parts[part] = (double)numerator / (double)denominator;
                }
                *out = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
                return true;
            };
            // The hemisphere is a one character ASCII tag, which does fit in
            // the entry, so it is read in place.
            auto readHemisphere = [&](size_t entry) -> char {
                if (read16(entry + 2) != 2 || read32(entry + 4) != 2) {
                    return 0;
                }
                size_t valueOffset = entry + 8;
                return (valueOffset < tiffLength) ? (char)tiff[valueOffset] : (char)0;
            };

            unsigned ifdOffset = read32(4);
            unsigned entryCount = read16(ifdOffset);
            unsigned exifIfdOffset = 0;
            unsigned gpsIfdOffset = 0;
            for (unsigned i = 0; i < entryCount; ++i) {
                size_t entry = (size_t)ifdOffset + 2 + (size_t)i * 12;
                if (entry + 12 > tiffLength) {
                    break;
                }
                unsigned tag = read16(entry);
                if (tag == 0x0112) {
                    unsigned orientation = read16(entry + 8);
                    switch (orientation) {
                    case 6:
                        info.rotationDegrees = 90.0f;
                        break;
                    case 3:
                        info.rotationDegrees = 180.0f;
                        break;
                    case 8:
                        info.rotationDegrees = 270.0f;
                        break;
                    default:
                        info.rotationDegrees = 0.0f;
                        break;
                    }
                } else if (tag == 0x0132) {
                    // DateTime is when the file was last written, so it is only
                    // a fallback for the shot time below.
                    info.dateTakenMs = readDate(entry);
                } else if (tag == 0x8769) {
                    exifIfdOffset = read32(entry + 8);
                } else if (tag == 0x8825) {
                    gpsIfdOffset = read32(entry + 8);
                }
            }

            entryCount = exifIfdOffset ? read16(exifIfdOffset) : 0;
            for (unsigned i = 0; i < entryCount; ++i) {
                size_t entry = (size_t)exifIfdOffset + 2 + (size_t)i * 12;
                if (entry + 12 > tiffLength) {
                    break;
                }
                if (read16(entry) == 0x9003) {
                    int64_t taken = readDate(entry);
                    if (taken != 0) {
                        info.dateTakenMs = taken;
                    }
                    break;
                }
            }

            double latitude = 0.0;
            double longitude = 0.0;
            char latitudeRef = 0;
            char longitudeRef = 0;
            bool haveLatitude = false;
            bool haveLongitude = false;
            entryCount = gpsIfdOffset ? read16(gpsIfdOffset) : 0;
            for (unsigned i = 0; i < entryCount; ++i) {
                size_t entry = (size_t)gpsIfdOffset + 2 + (size_t)i * 12;
                if (entry + 12 > tiffLength) {
                    break;
                }
                switch (read16(entry)) {
                case 0x0001:
                    latitudeRef = readHemisphere(entry);
                    break;
                case 0x0002:
                    haveLatitude = readCoordinate(entry, &latitude);
                    break;
                case 0x0003:
                    longitudeRef = readHemisphere(entry);
                    break;
                case 0x0004:
                    haveLongitude = readCoordinate(entry, &longitude);
                    break;
                default:
                    break;
                }
            }
            if (haveLatitude && haveLongitude) {
                if (latitudeRef == 'S') {
                    latitude = -latitude;
                }
                if (longitudeRef == 'W') {
                    longitude = -longitude;
                }
                // Exactly zero is how the rest of the port spells "no position",
                // so a reading on the equator or the meridian is nudged rather
                // than silently discarded.
                info.latitude = (latitude == 0.0) ? 1e-9 : latitude;
                info.longitude = (longitude == 0.0) ? 1e-9 : longitude;
            }
            break;
        }
        if (marker == 0xDA) {
            break;
        }
        pos += 2 + length;
    }
    return info;
}
