// Thumbnails from the freedesktop.org cache, which the file managers of the
// Linux desktops fill. Nothing here makes a thumbnail: a photo no file manager
// has shown yet is decoded instead. No macOS program fills the cache, so
// lookups there miss unless something else has.
#include "graphics/SystemThumbnail.h"

#include "graphics/ThumbnailCache.h"

bool SystemThumbnail::supported() {
    return true;
}

Bitmap SystemThumbnail::load(const std::string &path, int maxEdge) {
    const Bitmap upright = ThumbnailCache::loadUpright(ThumbnailCache::root(), path, maxEdge);
    if (!upright.valid()) {
        return Bitmap();
    }
    // Thumbnailers save the picture upright. The EXIF tag says how it is
    // shown, and is read only once there is a thumbnail to turn back.
    return upright.toStoredOrientation(Bitmap::readExif(path).orientation);
}
