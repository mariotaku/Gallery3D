// The thumbnail the platform keeps for a photo file. On Windows that is the one
// Explorer shows, from its thumbnail cache or made by the format's thumbnail
// handler, which for camera RAW and HEIF costs less than a decode. Linux and
// macOS read the freedesktop.org cache that file managers fill.
#pragma once

#include <string>

#include "graphics/Bitmap.h"

namespace SystemThumbnail {

// Whether this platform has a thumbnail store to ask. Without one, load always
// gives an invalid Bitmap.
bool supported();

// The picture at path the way a decode of the file gives it. The contract, on
// every platform, checked by tests/test_system_thumbnail.cpp:
// - Pixels are in the orientation they are stored in, whatever EXIF
//   orientation the file has: the store's upright picture is turned and
//   flipped back.
// - The store's picture is reduced by Bitmap::sampleSizeFor, keeping picked
//   pixels, so the long edge reaches maxEdge and is under twice it.
// - Premultiplied in Bitmap::decodeOrder(), and marked opaque when nothing in
//   it is transparent.
// - Invalid when the store has no thumbnail that reaches maxEdge, for a
//   maxEdge of 0 or below, and for a missing file. A file kept online only is
//   answered from the cache alone, so asking never downloads it.
Bitmap load(const std::string &path, int maxEdge);

}  // namespace SystemThumbnail
