// Decodes encoded bytes to Bitmap pixels for cropping, blur and caching.
// Native SDL_image answers inline; browser decoders require asynchronous callbacks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "Bitmap.h"

namespace ImageDecode {

// Called with the decoded image, or with an invalid Bitmap if it could not be
// decoded. May run on the calling thread before decode() returns, or later on
// another one.
using Callback = std::function<void(Bitmap)>;

// Decodes `bytes`, downscaled so neither edge exceeds maxEdge (0 for no limit).
// Takes ownership of the bytes, since the web has to keep them alive until the
// browser is finished with them.
void decode(std::vector<uint8_t> bytes, int maxEdge, Callback done);

// Whether decode() may invoke its callback after returning.
bool isAsynchronous();

}  // namespace ImageDecode
