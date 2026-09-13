#include "graphics/RegionDecoder.h"

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
