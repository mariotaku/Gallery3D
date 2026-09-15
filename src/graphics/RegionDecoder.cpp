#include "graphics/RegionDecoder.h"

#include <cstdint>

Bitmap RegionDecoder::decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) {
    // In 64 bits, so a rectangle near INT_MAX does not wrap round to inside.
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0 ||
        (int64_t)x + width > mWidth || (int64_t)y + height > mHeight) {
        return Bitmap();
    }
    Bitmap tile = decode(x, y, width, height, outWidth, outHeight);
    if (tile.valid() && (tile.width() != outWidth || tile.height() != outHeight)) {
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
