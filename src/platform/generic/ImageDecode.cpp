#include "graphics/ImageDecode.h"

#include <utility>

namespace ImageDecode {

bool isAsynchronous() {
    return false;
}

void decode(std::vector<uint8_t> bytes, int maxEdge, SampleFit fit, Callback done) {
    // Bitmap decodes on the calling thread and answers before returning.
    Bitmap bitmap = Bitmap::loadFromMemory(bytes.data(), bytes.size(), maxEdge, fit);
    if (done) {
        done(std::move(bitmap));
    }
}

}  // namespace ImageDecode
