// The freedesktop.org thumbnail cache, which GNOME, KDE and most Linux file
// managers share: one PNG per photo, named by the MD5 of the photo's file URI,
// in a folder per size. The specification is in xdg-specs, under thumbnail/.
#pragma once

#include <string>

#include "graphics/Bitmap.h"

namespace ThumbnailCache {

// The folder that holds the size folders: $XDG_CACHE_HOME/thumbnails, or
// ~/.cache/thumbnails when that is unset or blank. Empty when neither is known.
std::string root();

// The URI GLib's g_filename_to_uri gives an absolute path, which is the text a
// thumbnail is named by.
std::string uriFor(const std::string &path);

// The thumbnail cached under root for the photo at path, as the thumbnailer
// saved it, upright, and reduced by Bitmap::sampleSizeFor for maxEdge. Invalid
// when no size folder holds one that reaches maxEdge, or when the photo was
// changed after its thumbnail was made.
Bitmap loadUpright(const std::string &root, const std::string &path, int maxEdge);

// An upright picture turned back to how its pixels are stored, for a photo
// whose EXIF tag asks for a clockwise turn of degrees.
Bitmap turnedBack(const Bitmap &upright, float degrees);

}  // namespace ThumbnailCache
