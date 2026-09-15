#include "graphics/ColorProfile.h"

#include <algorithm>
#include <cmath>

// No ICC engine is linked here. Windows converts inside WIC. On Android an
// embedded profile is BitmapFactory's to convert, and on iOS a picture keeps
// the file's own colours. Adobe RGB named by EXIF alone is converted below with
// fixed matrices, since BitmapFactory ignores that tag.
bool ColorProfile::iccToSrgb(const std::vector<uint8_t> &profile, uint8_t *rgba, size_t count) {
    (void)profile;
    (void)rgba;
    (void)count;
    return false;
}

namespace {

// Adobe RGB (1998) as its specification gives it, a D65 white, three primaries
// and a gamma of 563/256, to sRGB. Both whites are D65, so the two matrices
// meet in CIE XYZ with no adaptation between them. Colours outside sRGB are
// clipped, as LittleCMS clips them on the desktop.
struct AdobeToSrgb {
    AdobeToSrgb() {
        for (int i = 0; i < 256; ++i) {
            decode[i] = std::pow(i / 255.0, 563.0 / 256.0);
        }
        for (int i = 0; i <= kEncodeSteps; ++i) {
            const double linear = (double)i / kEncodeSteps;
            const double encoded =
                linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
            encode[i] = (uint8_t)std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0);
        }
        static const double adobeToXyz[3][3] = {
            {0.5767309, 0.1855540, 0.1881852},
            {0.2973769, 0.6273491, 0.0752741},
            {0.0270343, 0.0706872, 0.9911085},
        };
        static const double xyzToSrgb[3][3] = {
            {3.2404542, -1.5371385, -0.4985314},
            {-0.9692660, 1.8760108, 0.0415560},
            {0.0556434, -0.2040259, 1.0572252},
        };
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                double sum = 0.0;
                for (int k = 0; k < 3; ++k) {
                    sum += xyzToSrgb[row][k] * adobeToXyz[k][column];
                }
                matrix[row][column] = sum;
            }
        }
    }

    // The sRGB encoding is looked up in this many steps of linear light.
    static constexpr int kEncodeSteps = 4096;

    double decode[256];
    uint8_t encode[kEncodeSteps + 1];
    double matrix[3][3];
};

}  // namespace

bool ColorProfile::adobeRgbToSrgb(uint8_t *rgba, size_t count) {
    if (rgba == nullptr || count == 0) {
        return false;
    }
    static const AdobeToSrgb table;
    for (size_t i = 0; i < count; ++i) {
        uint8_t *pixel = rgba + i * 4;
        const double linear[3] = {table.decode[pixel[0]], table.decode[pixel[1]], table.decode[pixel[2]]};
        for (int channel = 0; channel < 3; ++channel) {
            const double value = table.matrix[channel][0] * linear[0] + table.matrix[channel][1] * linear[1] +
                                 table.matrix[channel][2] * linear[2];
            const long step = std::lround(std::clamp(value, 0.0, 1.0) * AdobeToSrgb::kEncodeSteps);
            pixel[channel] = table.encode[step];
        }
    }
    return true;
}
