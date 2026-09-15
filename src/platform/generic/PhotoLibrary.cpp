#include "media/PhotoLibrary.h"

namespace PhotoLibrary {

PathStyle nativePathStyle() {
    return PathStyle::Posix;
}

bool isOnlineOnly(const std::string &path) {
    (void)path;
    return false;
}

Locations systemLocations() {
    // No library of the platform's own, which leaves the caller its one
    // Pictures folder.
    return Locations();
}

}  // namespace PhotoLibrary
