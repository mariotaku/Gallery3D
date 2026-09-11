// Turning encoded bytes into pixels, however the platform does it.
//
// Native decodes with SDL_image, on whichever thread asked, and returns the
// Bitmap. That is the shape the rest of the code was written around.
//
// The web cannot. Every image decoder a browser offers - createImageBitmap,
// WebCodecs, an <img> element - is a promise, and there is no synchronous one
// to fall back on. So the seam is asynchronous, and the native side answers
// immediately rather than the web side pretending to.
//
// Worth the trouble because the browser's decoder is native code with SIMD
// behind it, against stb_image compiled to wasm, and a wall is a hundred
// artworks.
//
// The pixels come back into memory rather than going straight to a texture,
// because the wall does its own work on them: the backdrop blurs them, the
// grid crops them to the cell's aspect, and DiskCache keeps the result. An
// ImageBitmap handed to texImage2D would skip all three.
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

// True when decode() may call back later rather than before it returns. Lets a
// caller that must not block know which world it is in.
bool isAsynchronous();

}  // namespace ImageDecode
