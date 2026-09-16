#include "graphics/SubsampledDecode.h"

void SubsampledDecode::init() {
}

Bitmap SubsampledDecode::decode(const void *bytes, size_t size, int maxEdge, SampleFit fit) {
    // No decoder here reduces while decoding, so the caller keeps its own
    // whole-image path.
    (void)bytes;
    (void)size;
    (void)maxEdge;
    (void)fit;
    return Bitmap();
}
