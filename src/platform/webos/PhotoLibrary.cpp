#include "media/PhotoLibrary.h"

#include "platform/webos/Storage.h"

namespace PhotoLibrary {

PathStyle nativePathStyle() {
    return PathStyle::Posix;
}

bool isOnlineOnly(const std::string &path) {
    (void)path;
    return false;
}

Locations systemLocations() {
    // The volumes the TV has attached, which is where its pictures are. A TV
    // has no home directory to fall back on, and the jail an app runs in
    // reaches nothing outside /tmp, /usr, /var and /media, so a volume mounted
    // anywhere else is listed here but reads as empty.
    Locations locations;
    locations.folders = withoutNested(Storage::mediaRoots());
    return locations;
}

}  // namespace PhotoLibrary
