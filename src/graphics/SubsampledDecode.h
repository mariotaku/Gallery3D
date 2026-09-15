// Decodes an encoded image straight to a reduced size.
//
// Decoding at full size and scaling after costs both ways: a 4000x3000 photo
// shown as a 512 pixel thumbnail is a 48MB surface and a full inverse transform,
// nearly all of which is then thrown away. Both backends here reduce inside the
// decoder, where it is close to free.
//
// The reductions are coarse, so each backend decodes at the smallest reduction
// that still covers the size asked for, then scales the last step itself: only
// it knows the original's size, which the size rule is worked out from.
#pragma once

#include <cstddef>

#include "graphics/Bitmap.h"

namespace SubsampledDecode {

// Finds the Java helper and holds on to it. Android only, and it has to run on
// the thread SDL calls main() on: the decode threads attach to the vm without
// the app's class loader and cannot look the class up by name. Does nothing
// anywhere else.
void init();

// Decodes at Bitmap::fitWithin of the original's size and maxEdge, or whole
// when maxEdge is 0. Returns an invalid Bitmap when this build cannot decode
// the format or the size this way, which leaves the caller its whole-image
// path. The desktop's libjpeg also converts the JPEG's colour profile to sRGB.
// Android answers only a maxEdge above 0.
Bitmap decode(const void *bytes, size_t size, int maxEdge);

}  // namespace SubsampledDecode
