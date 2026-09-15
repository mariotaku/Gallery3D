#include "graphics/RegionDecoder.h"

bool RegionDecoder::looksSupported(const std::string &mimeType) {
    // open() refuses everything, so nothing is offered.
    (void)mimeType;
    return false;
}

void RegionDecoder::initAndroid() {
}

// No region decoder is linked: the iOS build takes no libjpeg. A zoomed photo
// stays on its screennail.
RegionDecoderPtr RegionDecoder::open(const std::string &source) {
    (void)source;
    return nullptr;
}
