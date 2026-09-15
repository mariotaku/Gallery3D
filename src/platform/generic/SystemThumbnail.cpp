#include "graphics/SystemThumbnail.h"

bool SystemThumbnail::supported() {
    return false;
}

Bitmap SystemThumbnail::load(const std::string &path, int maxEdge) {
    (void)path;
    (void)maxEdge;
    return Bitmap();
}
