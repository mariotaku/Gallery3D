#include "graphics/ColorProfile.h"

// No colour engine is linked here. Windows converts inside WIC. On Android and
// iOS a picture that goes through SDL_image keeps the file's own colours.
bool ColorProfile::iccToSrgb(const std::vector<uint8_t> &profile, uint8_t *rgba, size_t count) {
    (void)profile;
    (void)rgba;
    (void)count;
    return false;
}

bool ColorProfile::adobeRgbToSrgb(uint8_t *rgba, size_t count) {
    (void)rgba;
    (void)count;
    return false;
}
