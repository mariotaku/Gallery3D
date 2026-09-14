// The thumbnail the platform keeps for a photo file. On Windows that is the one
// Explorer shows, from its thumbnail cache or made by the format's thumbnail
// handler, which for camera RAW and HEIF costs less than a decode.
#pragma once

#include <string>

#include "graphics/Bitmap.h"

namespace SystemThumbnail {

// The picture at path, scaled so its long edge is maxEdge, in the orientation
// its pixels are stored in, as a decode gives it. Invalid when the platform has
// no thumbnail that reaches maxEdge. A file kept online only is answered from
// the cache alone, so asking never downloads it.
Bitmap load(const std::string &path, int maxEdge);

}  // namespace SystemThumbnail
