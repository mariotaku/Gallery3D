// The ICC profile a picture carries inside its file. A decoder that does not
// convert colours itself finds the profile here and hands it to ColorProfile.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace EmbeddedProfile {

// The profile embedded in a JPEG (APP2 segments), PNG (iCCP), WebP (ICCP) or
// TIFF (tag 34675). Empty when the picture carries none, or is none of these.
std::vector<uint8_t> of(const void *bytes, size_t size);

}  // namespace EmbeddedProfile
