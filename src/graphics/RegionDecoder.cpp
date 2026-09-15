#include "graphics/RegionDecoder.h"

#include <cstdint>

Bitmap RegionDecoder::decodeRegion(int x, int y, int width, int height, int sampleSize) {
    // In 64 bits, so a rectangle near INT_MAX does not wrap round to inside.
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || sampleSize <= 0 || (sampleSize & (sampleSize - 1)) != 0 ||
        (int64_t)x + width > mWidth || (int64_t)y + height > mHeight) {
        return Bitmap();
    }
    if (mSampling == Sampling::Rescaled) {
        // libwebp decodes from even coordinates, and Skia widens the rectangle
        // to start there rather than shift it.
        width += x & 1;
        height += y & 1;
        x &= ~1;
        y &= ~1;
    }
    const bool whole = x == 0 && y == 0 && width == mWidth && height == mHeight;
    const Bitmap::Size size = whole ? Bitmap::sampledSize(mSampling, width, height, sampleSize)
                                    : Bitmap::sampledRegionSize(width, height, sampleSize);
    Bitmap tile = decode(x, y, width, height, sampleSize, size);
    if (tile.valid() && (tile.width() != size.width || tile.height() != size.height)) {
        return Bitmap();
    }
    return tile;
}

RegionDecoderPtr RegionDecoderCache::get(const std::string &source) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mSource != source) {
        // Held across the open so that decode threads starting on the same
        // photo together read the image once between them rather than each.
        mDecoder = RegionDecoder::open(source);
        mSource = source;
    }
    return mDecoder;
}
