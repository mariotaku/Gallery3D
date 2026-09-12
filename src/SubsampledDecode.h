// Decodes an encoded image straight to a reduced size.
//
// Decoding at full size and scaling after costs both ways: a 4000x3000 photo
// shown as a 512 pixel thumbnail is a 48MB surface and a full inverse transform,
// nearly all of which is then thrown away. Both backends here reduce inside the
// decoder, where it is close to free.
//
// The reductions are coarse, so the result is the smallest the decoder offers
// that is still at least as large as asked for. The caller scales the last step.
#pragma once

#include <cstddef>

#include "Bitmap.h"

namespace SubsampledDecode {

// Finds the Java helper and holds on to it. Android only, and it has to run on
// the thread SDL calls main() on: the decode threads attach to the vm without
// the app's class loader and cannot look the class up by name. Does nothing
// anywhere else.
void init();

// Decodes so the result is no smaller than maxEdge on its long edge, at the
// largest reduction that holds. Returns an invalid Bitmap when this build
// cannot decode the format this way, which leaves the caller its whole-image
// path.
Bitmap decode(const void *bytes, size_t size, int maxEdge);

}  // namespace SubsampledDecode
