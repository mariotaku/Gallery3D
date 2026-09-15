// Thumbnails from the freedesktop.org cache, which the file managers of the
// Linux desktops fill. Nothing here makes a thumbnail: a photo no file manager
// has shown yet is decoded instead. macOS has no such cache, and every lookup
// there misses.
#include "graphics/SystemThumbnail.h"

#include "graphics/ThumbnailCache.h"

Bitmap SystemThumbnail::load(const std::string &path, int maxEdge) {
    Bitmap upright = ThumbnailCache::loadUpright(ThumbnailCache::root(), path, maxEdge);
    if (!upright.valid()) {
        return Bitmap();
    }
    // Thumbnailers save the picture upright. The EXIF tag says how it was
    // turned, and is read only once there is a thumbnail to turn back.
    return ThumbnailCache::turnedBack(upright, Bitmap::readExif(path).rotationDegrees);
}
