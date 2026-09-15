#include "decode_fixtures.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "tests.h"

namespace DecodeFixtures {

std::string folder() {
    return fixtureRoot() + "/decode/";
}

nlohmann::json manifest() {
    std::vector<uint8_t> bytes;
    if (!Bitmap::readFile(folder() + "manifest.json", &bytes)) {
        return nlohmann::json();
    }
    return nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
}

nlohmann::json all() {
    const nlohmann::json parsed = manifest();
    if (!parsed.is_object() || !parsed["fixtures"].is_array()) {
        return nlohmann::json::array();
    }
    return parsed["fixtures"];
}

std::string extensionOf(const std::string &file) {
    const size_t dot = file.find_last_of('.');
    return dot == std::string::npos ? std::string() : file.substr(dot);
}

std::string mimeTypeOf(const std::string &file) {
    std::string extension = extensionOf(file);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".tif" || extension == ".tiff") {
        return "image/tiff";
    }
    return extension.empty() ? std::string() : "image/" + extension.substr(1);
}

Rgba pixelAt(const Bitmap &bitmap, int x, int y) {
    const uint8_t *p = bitmap.pixels() + ((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4;
    return Rgba{p[bitmap.redOffset()], p[1], p[bitmap.blueOffset()], p[3]};
}

Rgba premultiplied(int r, int g, int b, int a) {
    auto times = [a](int c) { return (c * a + 127) / 255; };
    return Rgba{times(r), times(g), times(b), a};
}

std::string describe(const Rgba &c) {
    return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," +
           std::to_string(c.a) + ")";
}

bool near(const Rgba &got, const Rgba &expected, int tolerance) {
    if (std::abs(got.a - expected.a) > 1) {
        return false;
    }
    if (expected.a == 0) {
        return true;
    }
    return std::abs(got.r - expected.r) <= tolerance && std::abs(got.g - expected.g) <= tolerance &&
           std::abs(got.b - expected.b) <= tolerance;
}

void checkProbes(const std::string &what, const Bitmap &bitmap, const nlohmann::json &probes, int tolerance) {
    for (const nlohmann::json &probe : probes) {
        const int x = probe[0];
        const int y = probe[1];
        if (x >= bitmap.width() || y >= bitmap.height()) {
            continue;
        }
        const Rgba expected = premultiplied(probe[2], probe[3], probe[4], probe[5]);
        const Rgba got = pixelAt(bitmap, x, y);
        CHECK_DETAIL(near(got, expected, tolerance), what + " at " + std::to_string(x) + "," + std::to_string(y) +
                                                         ": got " + describe(got) + ", wanted " +
                                                         describe(expected));
    }
}

void checkGolden(const std::string &what, const Bitmap &bitmap, const std::string &goldenFile, int goldenWidth,
                 int originX, int originY, int tolerance) {
    std::vector<uint8_t> golden;
    const bool read = Bitmap::readFile(folder() + goldenFile, &golden);
    const size_t goldenHeight = goldenWidth > 0 ? golden.size() / 4 / (size_t)goldenWidth : 0;
    const bool fits = read && goldenWidth > 0 && golden.size() == (size_t)goldenWidth * goldenHeight * 4 &&
                      originX >= 0 && originY >= 0 && originX + bitmap.width() <= goldenWidth &&
                      (size_t)(originY + bitmap.height()) <= goldenHeight;
    CHECK_DETAIL(fits, what + ": golden " + goldenFile + " missing or smaller than the bitmap");
    if (!fits) {
        return;
    }
    int mismatches = 0;
    int worst = 0;
    std::string first;
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            const uint8_t *g =
                &golden[((size_t)(y + originY) * (size_t)goldenWidth + (size_t)(x + originX)) * 4];
            const Rgba expected = premultiplied(g[0], g[1], g[2], g[3]);
            const Rgba got = pixelAt(bitmap, x, y);
            if (!near(got, expected, tolerance)) {
                worst = std::max({worst, std::abs(got.r - expected.r), std::abs(got.g - expected.g),
                                  std::abs(got.b - expected.b)});
                if (mismatches++ == 0) {
                    first = std::to_string(x) + "," + std::to_string(y) + " got " + describe(got) + ", wanted " +
                            describe(expected);
                }
            }
        }
    }
    CHECK_DETAIL(mismatches == 0, what + ": " + std::to_string(mismatches) + " pixels differ from " + goldenFile +
                                      " by up to " + std::to_string(worst) + ", first at " + first);
}

}  // namespace DecodeFixtures
