// What the decode tests share: the fixtures tools/fixtures/make_decode_fixtures.py
// wrote into tests/fixtures/decode, their manifest, and comparing decoded
// pixels with what the manifest expects.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "graphics/Bitmap.h"

namespace DecodeFixtures {

// The folder the fixtures are in, with a trailing slash.
std::string folder();

// The manifest, or null when it cannot be read.
nlohmann::json manifest();

// The fixtures listed in the manifest, or an empty array.
nlohmann::json all();

// The file's extension with its dot, such as ".jpg".
std::string extensionOf(const std::string &file);

// The mime type a data source gives a file with the fixture's extension.
std::string mimeTypeOf(const std::string &file);

struct Rgba {
    int r, g, b, a;
};

Rgba pixelAt(const Bitmap &bitmap, int x, int y);

// A straight colour premultiplied the way Bitmap::premultiply rounds it.
Rgba premultiplied(int r, int g, int b, int a);

std::string describe(const Rgba &c);

// Whether a decoded pixel is within tolerance of the expected one. Alpha has to
// be exact but for one level of rounding. Colour under no alpha means nothing
// and is not compared.
bool near(const Rgba &got, const Rgba &expected, int tolerance);

// Checks the manifest's probes, [x, y, r, g, b, a] in straight colour, against
// the bitmap. A probe outside the bitmap is not checked.
void checkProbes(const std::string &what, const Bitmap &bitmap, const nlohmann::json &probes, int tolerance);

// Checks every pixel of the bitmap against the rectangle of the golden that
// starts at originX, originY. The golden is straight RGBA of goldenWidth.
void checkGolden(const std::string &what, const Bitmap &bitmap, const std::string &goldenFile, int goldenWidth,
                 int originX, int originY, int tolerance);

}  // namespace DecodeFixtures
