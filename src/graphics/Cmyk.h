// Four channel pictures with no CMYK profile, converted without colour
// management. Every platform's decoder converts such a picture this way, so its
// colours do not depend on the CMYK profile a system happens to have.
#pragma once

#include <cstddef>
#include <cstdint>

#include "graphics/Bitmap.h"

namespace Cmyk {

// Converts count pixels of C, M, Y and K ink amounts, 0 to 255 each, to opaque
// pixels in order. `inverted` is for values stored as 255 minus the ink, as an
// Adobe marker says Photoshop writes them. `inks` and `pixels` may be the same
// buffer.
inline void toPixels(const uint8_t *inks, uint8_t *pixels, size_t count, PixelOrder order, bool inverted) {
    const int red = order == PixelOrder::RGBA ? 0 : 2;
    const int blue = 2 - red;
    for (size_t i = 0; i < count; ++i, inks += 4, pixels += 4) {
        const unsigned c = inverted ? 255u - inks[0] : inks[0];
        const unsigned m = inverted ? 255u - inks[1] : inks[1];
        const unsigned y = inverted ? 255u - inks[2] : inks[2];
        const unsigned k = inverted ? 255u - inks[3] : inks[3];
        pixels[red] = (uint8_t)(((255u - c) * (255u - k) + 127u) / 255u);
        pixels[1] = (uint8_t)(((255u - m) * (255u - k) + 127u) / 255u);
        pixels[blue] = (uint8_t)(((255u - y) * (255u - k) + 127u) / 255u);
        pixels[3] = 255;
    }
}

}  // namespace Cmyk
