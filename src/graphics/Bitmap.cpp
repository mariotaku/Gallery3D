// Bitmap's pixel arithmetic and EXIF reader. Decoding and PNG writing are the
// platform's, in BitmapDecode.cpp under src/platform/.
#include "graphics/Bitmap.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

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
void premultiplyPixels(uint8_t *pixels, size_t count) {
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

}  // namespace

void Bitmap::reorder(PixelOrder order) {
    if (order == mOrder) {
        return;
    }
    mOrder = order;
    uint8_t *pixel = mPixels.data();
    const uint8_t *end = pixel + mPixels.size();
    for (; pixel < end; pixel += 4) {
        std::swap(pixel[0], pixel[2]);
    }
}

Bitmap Bitmap::inOrder(PixelOrder order) const {
    Bitmap result = *this;
    result.reorder(order);
    return result;
}

Bitmap::Bitmap(int width, int height, PixelOrder order) : mWidth(width), mHeight(height), mOrder(order) {
    if (width > 0 && height > 0) {
        mPixels.assign((size_t)width * (size_t)height * 4, 0);
    } else {
        mWidth = 0;
        mHeight = 0;
    }
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

void Bitmap::premultiply() {
    premultiplyPixels(mPixels.data(), (size_t)mWidth * (size_t)mHeight);
}

namespace {

// The source pixels each output pixel takes along one axis, with weights in
// 1/16384ths that add up to one. Taps for output pixel i are
// indices[starts[i]] to indices[starts[i + 1] - 1].
struct Taps {
    std::vector<int> starts;
    std::vector<int> indices;
    std::vector<uint32_t> weights;
};

constexpr uint32_t kWeightOne = 1u << 14;

Taps tapsFor(int from, int to) {
    Taps taps;
    taps.starts.reserve((size_t)to + 1);
    const double scale = (double)from / (double)to;
    for (int i = 0; i < to; ++i) {
        taps.starts.push_back((int)taps.indices.size());
        const size_t first = taps.weights.size();
        if (scale >= 1.0) {
            // Shrinking: the average of the span the output pixel covers,
            // with the pixels at its ends weighted by how much of them it
            // covers.
            const double left = i * scale;
            const double right = (i + 1) * scale;
            for (int x = (int)left; x < from && x < right; ++x) {
                const double covered = std::min(right, x + 1.0) - std::max(left, (double)x);
                if (covered > 0.0) {
                    taps.indices.push_back(x);
                    taps.weights.push_back((uint32_t)std::lround(covered / scale * kWeightOne));
                }
            }
        } else {
            // Enlarging: between the two nearest pixel centres, so the picture
            // does not shift by half a pixel.
            const double centre = std::max(0.0, (i + 0.5) * scale - 0.5);
            const int x = std::min((int)centre, from - 1);
            const double fraction = (x + 1 < from) ? centre - x : 0.0;
            taps.indices.push_back(x);
            taps.weights.push_back((uint32_t)std::lround((1.0 - fraction) * kWeightOne));
            if (fraction > 0.0) {
                taps.indices.push_back(x + 1);
                taps.weights.push_back((uint32_t)std::lround(fraction * kWeightOne));
            }
        }
        // Rounding can leave the weights a little off one, which would darken
        // or brighten a flat colour. The heaviest tap takes the difference.
        uint32_t sum = 0;
        size_t heaviest = first;
        for (size_t t = first; t < taps.weights.size(); ++t) {
            sum += taps.weights[t];
            if (taps.weights[t] > taps.weights[heaviest]) {
                heaviest = t;
            }
        }
        taps.weights[heaviest] += kWeightOne - sum;
    }
    taps.starts.push_back((int)taps.indices.size());
    return taps;
}

}  // namespace

Bitmap Bitmap::scaled(int newWidth, int newHeight) const {
    if (!valid() || newWidth <= 0 || newHeight <= 0) {
        return Bitmap();
    }
    if (newWidth == mWidth && newHeight == mHeight) {
        return *this;
    }
    const Taps across = tapsFor(mWidth, newWidth);
    const Taps down = tapsFor(mHeight, newHeight);

    // Down first, whole rows at a time, which reads memory in order and lets
    // the compiler vectorize the sums. Then across the fewer rows left. The
    // weights add up to one, so no sum passes 255 and nothing is clamped.
    // Premultiplied pixels average without a transparent pixel lending its
    // colour.
    const size_t sourceStride = (size_t)mWidth * 4;
    std::vector<uint8_t> rows(sourceStride * (size_t)newHeight);
    std::vector<uint32_t> sums(sourceStride);
    for (int y = 0; y < newHeight; ++y) {
        std::fill(sums.begin(), sums.end(), kWeightOne / 2);
        for (int t = down.starts[(size_t)y]; t < down.starts[(size_t)y + 1]; ++t) {
            const uint8_t *row = mPixels.data() + (size_t)down.indices[(size_t)t] * sourceStride;
            const uint32_t weight = down.weights[(size_t)t];
            uint32_t *sum = sums.data();
            for (size_t i = 0; i < sourceStride; ++i) {
                sum[i] += row[i] * weight;
            }
        }
        uint8_t *target = rows.data() + (size_t)y * sourceStride;
        for (size_t i = 0; i < sourceStride; ++i) {
            target[i] = (uint8_t)(sums[i] >> 14);
        }
    }

    Bitmap result(newWidth, newHeight, mOrder);
    result.mOpaque = mOpaque;
    for (int y = 0; y < newHeight; ++y) {
        const uint8_t *source = rows.data() + (size_t)y * sourceStride;
        uint8_t *target = result.pixels() + (size_t)y * (size_t)newWidth * 4;
        for (int x = 0; x < newWidth; ++x) {
            uint32_t sum[4] = {kWeightOne / 2, kWeightOne / 2, kWeightOne / 2, kWeightOne / 2};
            for (int t = across.starts[(size_t)x]; t < across.starts[(size_t)x + 1]; ++t) {
                const uint8_t *pixel = source + (size_t)across.indices[(size_t)t] * 4;
                const uint32_t weight = across.weights[(size_t)t];
                for (int channel = 0; channel < 4; ++channel) {
                    sum[channel] += pixel[channel] * weight;
                }
            }
            for (int channel = 0; channel < 4; ++channel) {
                target[x * 4 + channel] = (uint8_t)(sum[channel] >> 14);
            }
        }
    }
    return result;
}

Bitmap Bitmap::paddedTo(int paddedWidth, int paddedHeight, bool clampEdges) const {
    if (!valid() || paddedWidth < mWidth || paddedHeight < mHeight) {
        return *this;
    }
    Bitmap result(paddedWidth, paddedHeight, mOrder);
    // Repeated edges are as opaque as the picture. Transparent padding is not.
    result.mOpaque = mOpaque && clampEdges;
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

bool Bitmap::hasTransparency() const {
    if (!valid() || mOpaque) {
        return false;
    }
    const size_t count = (size_t)mWidth * (size_t)mHeight;
    const uint8_t *pixels = mPixels.data();
    for (size_t i = 0; i < count; ++i) {
        if (pixels[i * 4 + 3] != 255) {
            return true;
        }
    }
    return false;
}

void Bitmap::markOpaqueUnlessTransparent() {
    if (valid() && !mOpaque && !hasTransparency()) {
        mOpaque = true;
    }
}

namespace {

// Skia's get_scaled_dimension: rounded down, and 1 when the sample is larger.
int roundedDown(int dimension, int sampleSize) {
    return sampleSize > dimension ? 1 : dimension / sampleSize;
}

// libjpeg's reduced size, rounded up, then picked from past a reduction of 8
// as SkSampledCodec does.
int jpegSampled(int dimension, int sampleSize) {
    const int native = std::min(sampleSize, 8);
    const int reduced = (dimension + native - 1) / native;
    const int rest = sampleSize / native;
    return rest == 1 ? reduced : roundedDown(reduced, rest);
}

}  // namespace

int Bitmap::sampleSizeFor(int width, int height, int maxEdge, SampleFit fit) {
    const int longEdge = std::max(width, height);
    int sampleSize = 1;
    if (maxEdge <= 0 || longEdge <= 0) {
        return sampleSize;
    }
    if (fit == SampleFit::Under) {
        while (sampleSize < (1 << 30) && longEdge / sampleSize > maxEdge) {
            sampleSize *= 2;
        }
        return sampleSize;
    }
    while (sampleSize < (1 << 30) && longEdge / (sampleSize * 2) >= maxEdge) {
        sampleSize *= 2;
    }
    return sampleSize;
}

Bitmap::Size Bitmap::sampledSize(Sampling sampling, int width, int height, int sampleSize) {
    if (sampleSize <= 1 || width <= 0 || height <= 0) {
        return Size{width, height};
    }
    switch (sampling) {
    case Sampling::Jpeg:
        return Size{jpegSampled(width, sampleSize), jpegSampled(height, sampleSize)};
    case Sampling::Rescaled:
        // SkScalingCodec: the scale times the size, rounded to nearest.
        return Size{std::max(1, (int)std::floor((float)width / (float)sampleSize + 0.5f)),
                    std::max(1, (int)std::floor((float)height / (float)sampleSize + 0.5f))};
    case Sampling::Picked:
    default:
        return Size{roundedDown(width, sampleSize), roundedDown(height, sampleSize)};
    }
}

Bitmap::Size Bitmap::sampledRegionSize(int width, int height, int sampleSize) {
    if (sampleSize <= 1 || width <= 0 || height <= 0) {
        return Size{width, height};
    }
    return Size{roundedDown(width, sampleSize), roundedDown(height, sampleSize)};
}

Sampling Bitmap::samplingOf(const void *bytes, size_t size) {
    const unsigned char *data = (const unsigned char *)bytes;
    if (data != nullptr && size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return Sampling::Jpeg;
    }
    if (data != nullptr && size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0) {
        return Sampling::Rescaled;
    }
    return Sampling::Picked;
}

Sampling Bitmap::samplingOfMimeType(const std::string &mimeType) {
    if (mimeType == "image/jpeg") {
        return Sampling::Jpeg;
    }
    if (mimeType == "image/webp") {
        return Sampling::Rescaled;
    }
    return Sampling::Picked;
}

Bitmap Bitmap::picked(int newWidth, int newHeight) const {
    if (!valid() || newWidth <= 0 || newHeight <= 0 || newWidth > mWidth || newHeight > mHeight) {
        return Bitmap();
    }
    if (newWidth == mWidth && newHeight == mHeight) {
        return *this;
    }
    const int sampleX = mWidth / newWidth;
    const int sampleY = mHeight / newHeight;
    Bitmap result(newWidth, newHeight, mOrder);
    result.mOpaque = mOpaque;
    for (int y = 0; y < newHeight; ++y) {
        const uint8_t *row = mPixels.data() + (size_t)(y * sampleY + sampleY / 2) * (size_t)mWidth * 4;
        uint8_t *target = result.pixels() + (size_t)y * (size_t)newWidth * 4;
        for (int x = 0; x < newWidth; ++x) {
            std::memcpy(target + (size_t)x * 4, row + (size_t)(x * sampleX + sampleX / 2) * 4, 4);
        }
    }
    return result;
}

Bitmap Bitmap::toStoredOrientation(int orientation) const {
    if (!valid() || orientation < 2 || orientation > 8) {
        return *this;
    }
    // 5 to 8 turn the picture a quarter, so the stored one is the other way
    // round.
    const bool quarter = orientation >= 5;
    const int storedWidth = quarter ? mHeight : mWidth;
    const int storedHeight = quarter ? mWidth : mHeight;
    const int w = storedWidth;
    const int h = storedHeight;
    Bitmap stored(storedWidth, storedHeight, mOrder);
    stored.mOpaque = mOpaque;
    for (int sy = 0; sy < h; ++sy) {
        for (int sx = 0; sx < w; ++sx) {
            // Where the stored pixel shows once the orientation is applied.
            int ux = sx;
            int uy = sy;
            switch (orientation) {
            case 2:  // mirrored left to right
                ux = w - 1 - sx;
                break;
            case 3:  // half a turn
                ux = w - 1 - sx;
                uy = h - 1 - sy;
                break;
            case 4:  // mirrored top to bottom
                uy = h - 1 - sy;
                break;
            case 5:  // mirrored across the diagonal from the top left
                ux = sy;
                uy = sx;
                break;
            case 6:  // a quarter turn clockwise
                ux = h - 1 - sy;
                uy = sx;
                break;
            case 7:  // mirrored across the other diagonal
                ux = h - 1 - sy;
                uy = w - 1 - sx;
                break;
            case 8:  // a quarter turn anticlockwise
                ux = sy;
                uy = w - 1 - sx;
                break;
            default:
                break;
            }
            std::memcpy(stored.pixels() + ((size_t)sy * (size_t)w + (size_t)sx) * 4,
                        mPixels.data() + ((size_t)uy * (size_t)mWidth + (size_t)ux) * 4, 4);
        }
    }
    return stored;
}

Bitmap Bitmap::sampledFromWhole(Sampling sampling, int sampleSize) const {
    if (!valid() || sampleSize <= 1) {
        return *this;
    }
    const Size size = sampledSize(sampling, mWidth, mHeight, sampleSize);
    switch (sampling) {
    case Sampling::Jpeg: {
        const int native = std::min(sampleSize, 8);
        const Bitmap reduced = scaled((mWidth + native - 1) / native, (mHeight + native - 1) / native);
        return reduced.picked(size.width, size.height);
    }
    case Sampling::Rescaled:
        return scaled(size.width, size.height);
    case Sampling::Picked:
    default:
        return picked(size.width, size.height);
    }
}

Bitmap Bitmap::cropped(int x, int y, int width, int height) const {
    if (!valid() || x < 0 || y < 0 || width <= 0 || height <= 0 || width > mWidth - x || height > mHeight - y) {
        return Bitmap();
    }
    Bitmap result(width, height, mOrder);
    result.mOpaque = mOpaque;
    for (int row = 0; row < height; ++row) {
        std::memcpy(result.pixels() + (size_t)row * (size_t)width * 4,
                    mPixels.data() + ((size_t)(y + row) * (size_t)mWidth + (size_t)x) * 4, (size_t)width * 4);
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
    Bitmap result(newWidth, newHeight, mOrder);
    result.mOpaque = mOpaque;
    for (int y = 0; y < newHeight; ++y) {
        std::memcpy(result.pixels() + (size_t)y * (size_t)newWidth * 4,
                    scaled.pixels() + ((size_t)(y + offsetY) * (size_t)scaledWidth + (size_t)offsetX) * 4,
                    (size_t)newWidth * 4);
    }
    return result;
}

float Bitmap::degreesForOrientation(unsigned orientation) {
    switch (orientation) {
    case 6:
        return 90.0f;
    case 3:
        return 180.0f;
    case 8:
        return 270.0f;
    default:
        return 0.0f;
    }
}

bool Bitmap::endsEarly(const void *bytes, size_t size) {
    const unsigned char *data = (const unsigned char *)bytes;
    if (data == nullptr) {
        return false;
    }
    static const unsigned char kPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (size >= 8 && std::memcmp(data, kPngSignature, 8) == 0) {
        const size_t tail = std::min<size_t>(size, 64);
        static const char kEnd[4] = {'I', 'E', 'N', 'D'};
        return std::search(data + size - tail, data + size, kEnd, kEnd + 4) == data + size;
    }
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    // Past the segments before the first scan, which can hold an EXIF
    // thumbnail with an end marker of its own.
    size_t at = 2;
    for (;;) {
        while (at < size && data[at] == 0xFF) {
            ++at;
        }
        if (at >= size) {
            return true;
        }
        const unsigned char marker = data[at++];
        if (marker == 0xDA) {
            break;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (at + 2 > size) {
            return true;
        }
        at += ((size_t)data[at] << 8) | data[at + 1];
    }
    // Entropy-coded data never holds 0xFF 0xD9, so the first one found from
    // the end is the image's own. Most files end with it, so the search is
    // short unless the file is cut.
    for (size_t end = size - 1; end > at; --end) {
        if (data[end] == 0xD9 && data[end - 1] == 0xFF) {
            return false;
        }
    }
    return true;
}

Bitmap::ExifInfo Bitmap::readExif(const std::string &path) {
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return ExifInfo();
    }
    // An APP1 segment is at most 64KB, and the markers before it are short.
    std::vector<uint8_t> header(65536);
    const size_t read = std::fread(header.data(), 1, header.size(), file);
    std::fclose(file);
    return readExif(header.data(), read);
}

bool Bitmap::sameShape(int width, int height, int otherWidth, int otherHeight) {
    if (width <= 0 || height <= 0 || otherWidth <= 0 || otherHeight <= 0) {
        return false;
    }
    const double shape = (double)width / (double)height;
    const double otherShape = (double)otherWidth / (double)otherHeight;
    return std::fabs(shape - otherShape) <= 0.02 * otherShape;
}

Bitmap::ExifInfo Bitmap::readExif(const void *bytes, size_t size) {
    // Read JPEG APP1 orientation (IFD0 0x0112), date (Exif IFD via 0x8769, tag 0x9003),
    // GPS (IFD via 0x8825) and the IFD1 thumbnail (0x0201, 0x0202). Bounds-check
    // all reads; malformed data keeps defaults.
    ExifInfo info;
    const uint8_t *header = (const uint8_t *)bytes;
    const size_t read = size;
    // A camera RAW in a TIFF container, such as ARW, CR2, DNG or NEF, keeps
    // its orientation in its first IFD, as a JPEG's APP1 does.
    if (bytes != nullptr && read >= 8 &&
        ((header[0] == 'I' && header[1] == 'I' && header[2] == 42 && header[3] == 0) ||
         (header[0] == 'M' && header[1] == 'M' && header[2] == 0 && header[3] == 42))) {
        const bool bigEndian = header[0] == 'M';
        auto read16 = [&](size_t offset) -> unsigned {
            if (offset + 2 > read) {
                return 0;
            }
            return bigEndian ? (unsigned)((header[offset] << 8) | header[offset + 1])
                             : (unsigned)((header[offset + 1] << 8) | header[offset]);
        };
        auto read32 = [&](size_t offset) -> unsigned {
            if (offset + 4 > read) {
                return 0;
            }
            if (bigEndian) {
                return ((unsigned)header[offset] << 24) | ((unsigned)header[offset + 1] << 16) |
                       ((unsigned)header[offset + 2] << 8) | (unsigned)header[offset + 3];
            }
            return ((unsigned)header[offset + 3] << 24) | ((unsigned)header[offset + 2] << 16) |
                   ((unsigned)header[offset + 1] << 8) | (unsigned)header[offset];
        };
        const unsigned ifdOffset = read32(4);
        const unsigned entryCount = read16(ifdOffset);
        for (unsigned i = 0; i < entryCount; ++i) {
            const size_t entry = (size_t)ifdOffset + 2 + (size_t)i * 12;
            if (entry + 12 > read) {
                break;
            }
            if (read16(entry) == 0x0112) {
                info.orientation = (int)read16(entry + 8);
                info.rotationDegrees = degreesForOrientation(read16(entry + 8));
            }
        }
        return info;
    }
    if (bytes == nullptr || read < 12 || header[0] != 0xFF || header[1] != 0xD8) {
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
                    info.orientation = (int)read16(entry + 8);
                    info.rotationDegrees = degreesForOrientation(read16(entry + 8));
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

            // IFD1 follows IFD0's entries and holds the thumbnail a camera
            // stores beside the photo: a JPEG at an offset from the TIFF header.
            const unsigned ifd1Offset = read32((size_t)ifdOffset + 2 + (size_t)entryCount * 12);
            const unsigned ifd1Count = ifd1Offset ? read16(ifd1Offset) : 0;
            unsigned thumbnailAt = 0;
            unsigned thumbnailSize = 0;
            for (unsigned i = 0; i < ifd1Count; ++i) {
                size_t entry = (size_t)ifd1Offset + 2 + (size_t)i * 12;
                if (entry + 12 > tiffLength) {
                    break;
                }
                const unsigned tag = read16(entry);
                if (tag == 0x0201) {
                    thumbnailAt = read32(entry + 8);
                } else if (tag == 0x0202) {
                    thumbnailSize = read32(entry + 8);
                }
            }
            if (thumbnailSize > 0 && thumbnailAt <= tiffLength && thumbnailSize <= tiffLength - thumbnailAt) {
                info.thumbnailOffset = (pos + 10) + thumbnailAt;
                info.thumbnailLength = thumbnailSize;
            }

            entryCount = exifIfdOffset ? read16(exifIfdOffset) : 0;
            for (unsigned i = 0; i < entryCount; ++i) {
                size_t entry = (size_t)exifIfdOffset + 2 + (size_t)i * 12;
                if (entry + 12 > tiffLength) {
                    break;
                }
                const unsigned tag = read16(entry);
                if (tag == 0x9003) {
                    int64_t taken = readDate(entry);
                    if (taken != 0) {
                        info.dateTakenMs = taken;
                    }
                } else if (tag == 0xA001) {
                    info.colorSpace = (int)read16(entry + 8);
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
        }
        // A start-of-frame marker carries the pixel size. The range holds the
        // baseline and progressive frames; the three gaps in it are the huffman
        // and arithmetic coding tables, which are not frames.
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC &&
            length >= 7) {
            info.pixelHeight = (int)(((unsigned)header[pos + 5] << 8) | header[pos + 6]);
            info.pixelWidth = (int)(((unsigned)header[pos + 7] << 8) | header[pos + 8]);
        }
        if (marker == 0xDA) {
            break;
        }
        pos += 2 + length;
    }
    return info;
}
