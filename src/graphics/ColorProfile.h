// Converting a picture's colours to sRGB, which the wall draws in, for the
// decoders that hand out the file's own colours. WIC converts inside its
// decodes on Windows. Linux and macOS convert through LittleCMS.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ColorProfile {

// Converts count straight RGBA pixels to sRGB in place, from an ICC profile,
// leaving alpha as it is. False, with the pixels untouched, when the profile is
// not an RGB one this platform can convert.
bool iccToSrgb(const std::vector<uint8_t> &profile, uint8_t *rgba, size_t count);

// The same from Adobe RGB (1998), for a photo that embeds no profile and whose
// EXIF ColorSpace is 2.
bool adobeRgbToSrgb(uint8_t *rgba, size_t count);

}  // namespace ColorProfile
