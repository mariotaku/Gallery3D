// Decodes encoded bytes to Bitmap pixels for cropping, blur and caching.
// Decoders answer through a callback, which today runs before decode() returns.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "graphics/Bitmap.h"

namespace ImageDecode {

// Called with the decoded image, or with an invalid Bitmap if it could not be
// decoded. May run on the calling thread before decode() returns, or later on
// another one.
using Callback = std::function<void(Bitmap)>;

// Decodes `bytes`, downscaled to maxEdge (0 for no limit) on the side of it
// that fit names. Takes ownership of the bytes.
void decode(std::vector<uint8_t> bytes, int maxEdge, SampleFit fit, Callback done);

// Whether decode() may invoke its callback after returning.
bool isAsynchronous();

}  // namespace ImageDecode
