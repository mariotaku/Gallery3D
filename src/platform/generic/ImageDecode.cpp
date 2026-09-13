#include "graphics/ImageDecode.h"

#include <utility>

namespace ImageDecode {

bool isAsynchronous() {
    return false;
}

void decode(std::vector<uint8_t> bytes, int maxEdge, Callback done) {
    // SDL_image decodes on the calling thread and answers before returning.
    Bitmap bitmap = Bitmap::loadFromMemory(bytes.data(), bytes.size(), maxEdge);
    if (done) {
        done(std::move(bitmap));
    }
}

}  // namespace ImageDecode
