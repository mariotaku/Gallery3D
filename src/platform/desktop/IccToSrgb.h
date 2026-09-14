// Converts pixels from the colour profile a JPEG embeds to sRGB, through
// LittleCMS. The wall draws in sRGB, so a Display P3 photo left as it is shows
// with the wrong colours.
#pragma once

#include <cstddef>
#include <cstdint>

class IccToSrgb {
  public:
    IccToSrgb() = default;
    ~IccToSrgb();
    IccToSrgb(const IccToSrgb &) = delete;
    IccToSrgb &operator=(const IccToSrgb &) = delete;

    // Builds the conversion from an ICC profile. Builds none when the profile
    // is not an RGB one LittleCMS can read, and the pixels keep their values.
    void open(const uint8_t *profile, size_t size);

    // Whether there is a conversion to make.
    explicit operator bool() const {
        return mTransform != nullptr;
    }

    // Converts count RGBA pixels in place, leaving alpha as it is. Safe to call
    // from several threads at once.
    void convert(uint8_t *rgba, size_t count) const;

  private:
    void *mTransform = nullptr;
};
