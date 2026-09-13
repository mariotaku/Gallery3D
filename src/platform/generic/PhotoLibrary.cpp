#include "media/PhotoLibrary.h"

namespace PhotoLibrary {

std::string comparable(const std::string &path) {
    // Case counts on these file systems, and a backslash is part of a name
    // rather than a separator, so only a trailing separator comes off.
    std::string spelled = path;
    // Keep the root itself whole: "/" is a folder in its own right.
    while (spelled.size() > 1 && spelled.back() == '/') {
        spelled.pop_back();
    }
    return spelled;
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
