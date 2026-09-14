#include "graphics/RegionDecoder.h"

bool RegionDecoder::looksSupported(const std::string &mimeType) {
    // The desktop's answer. open() is what refuses here, and a photo it refuses
    // stays on one downscaled decode.
    return mimeType == "image/jpeg";
}

void RegionDecoder::initAndroid() {
}

// No region decoder is linked: the browser decodes whole images only, and the
// iOS build takes no libjpeg. A zoomed local photo stays on its screennail.
RegionDecoderPtr RegionDecoder::open(const std::string &source) {
    (void)source;
    return nullptr;
}
