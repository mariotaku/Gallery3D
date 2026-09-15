// Every platform's decoder against the same files: the fixtures
// tools/fixtures/make_decode_fixtures.py wrote into tests/fixtures/decode, and
// what its manifest says a decode gives back.
//
// The contract, for Bitmap::load and Bitmap::loadFromMemory alike:
// - Pixels are premultiplied, in Bitmap::decodeOrder().
// - Pixels stay in the orientation they are stored in.
// - Colours are converted to sRGB from an embedded ICC profile, or from EXIF
//   ColorSpace 2 (Adobe RGB) when there is no profile.
// - A format with no alpha channel is marked opaque.
// - maxEdge scales the long edge to maxEdge and rounds the short edge to the
//   nearest pixel. A picture that already fits, or maxEdge 0 or below, keeps
//   its size.
// - A reduced decode may answer from an embedded thumbnail in the picture's
//   shape whose long edge reaches the size asked for.
// - A file that is cut short or is not an image does not decode.
#include "tests.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "graphics/Bitmap.h"

namespace {

std::string decodeFolder() {
    return fixtureRoot() + "/decode/";
}

nlohmann::json manifest() {
    std::vector<uint8_t> bytes;
    if (!Bitmap::readFile(decodeFolder() + "manifest.json", &bytes)) {
        return nlohmann::json();
    }
    return nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
}

std::string extensionOf(const std::string &file) {
    const size_t dot = file.find_last_of('.');
    return dot == std::string::npos ? std::string() : file.substr(dot);
}

// Whether this platform decodes the fixture's format at all. A format it has
// no decoder for is noted rather than failed; which formats every platform must
// decode is checked on its own.
bool decodable(const nlohmann::json &fixture) {
    const std::string file = fixture["file"];
    if (Bitmap::decodesExtension(extensionOf(file))) {
        return true;
    }
    reportNote(file + ": no decoder for " + extensionOf(file) + " here");
    return false;
}

struct Rgba {
    int r, g, b, a;
};

Rgba pixelAt(const Bitmap &bitmap, int x, int y) {
    const uint8_t *p = bitmap.pixels() + ((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4;
    return Rgba{p[bitmap.redOffset()], p[1], p[bitmap.blueOffset()], p[3]};
}

// A straight colour premultiplied the way Bitmap::premultiply rounds it.
Rgba premultiplied(int r, int g, int b, int a) {
    auto times = [a](int c) { return (c * a + 127) / 255; };
    return Rgba{times(r), times(g), times(b), a};
}

std::string describe(const Rgba &c) {
    return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," +
           std::to_string(c.a) + ")";
}

// Whether a decoded pixel is within tolerance of the expected straight colour.
// Alpha has to be exact but for one level of rounding. Colour under no alpha
// means nothing and is not compared.
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

void checkGolden(const std::string &what, const Bitmap &bitmap, const std::string &goldenFile, int tolerance) {
    std::vector<uint8_t> golden;
    const bool read = Bitmap::readFile(decodeFolder() + goldenFile, &golden);
    CHECK_DETAIL(read && golden.size() == (size_t)bitmap.width() * (size_t)bitmap.height() * 4,
                 what + ": golden " + goldenFile + " missing or the wrong size");
    if (!read || golden.size() != (size_t)bitmap.width() * (size_t)bitmap.height() * 4) {
        return;
    }
    int mismatches = 0;
    std::string first;
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            const uint8_t *g = &golden[((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4];
            const Rgba expected = premultiplied(g[0], g[1], g[2], g[3]);
            const Rgba got = pixelAt(bitmap, x, y);
            if (!near(got, expected, tolerance)) {
                if (mismatches++ == 0) {
                    first = std::to_string(x) + "," + std::to_string(y) + " got " + describe(got) + ", wanted " +
                            describe(expected);
                }
            }
        }
    }
    CHECK_DETAIL(mismatches == 0, what + ": " + std::to_string(mismatches) + " pixels differ from " + goldenFile +
                                      ", first at " + first);
}

// What every decode of the fixture has to be, whatever its size.
void checkForm(const std::string &what, const Bitmap &bitmap, const nlohmann::json &fixture) {
    CHECK_DETAIL(bitmap.order() == Bitmap::decodeOrder(), what + ": pixels not in decodeOrder()");
    if (!fixture["alpha"].get<bool>()) {
        CHECK_DETAIL(bitmap.knownOpaque(), what + ": a format with no alpha is not marked opaque");
    } else {
        CHECK_DETAIL(!bitmap.knownOpaque() && bitmap.hasTransparency(),
                     what + ": a picture with transparent pixels decodes opaque");
    }
}

}  // namespace

TEST(the_decode_fixtures_are_there) {
    const nlohmann::json fixtures = manifest();
    CHECK_DETAIL(fixtures.is_object() && fixtures["fixtures"].is_array() && !fixtures["fixtures"].empty(),
                 "no manifest at " + decodeFolder() + "manifest.json");
}

TEST(every_platform_decodes_jpeg_png_and_bmp) {
    CHECK(Bitmap::decodesExtension(".jpg"));
    CHECK(Bitmap::decodesExtension(".JPG"));
    CHECK(Bitmap::decodesExtension(".jpeg"));
    CHECK(Bitmap::decodesExtension(".png"));
    CHECK(Bitmap::decodesExtension(".bmp"));
    CHECK(!Bitmap::decodesExtension(""));
    CHECK(!Bitmap::decodesExtension("jpg"));
    CHECK(!Bitmap::decodesExtension(".txt"));
}

TEST(a_fixture_decodes_whole_to_its_expected_pixels) {
    for (const nlohmann::json &fixture : manifest().value("fixtures", nlohmann::json::array())) {
        if (fixture.value("invalid", false) || !decodable(fixture)) {
            continue;
        }
        const std::string file = fixture["file"];
        const Bitmap bitmap = Bitmap::load(decodeFolder() + file, 0);
        CHECK_DETAIL(bitmap.valid(), file + ": did not decode");
        if (!bitmap.valid()) {
            continue;
        }
        CHECK_DETAIL(bitmap.width() == fixture["width"].get<int>() && bitmap.height() == fixture["height"].get<int>(),
                     file + ": decoded " + std::to_string(bitmap.width()) + "x" + std::to_string(bitmap.height()));
        if (bitmap.width() != fixture["width"].get<int>() || bitmap.height() != fixture["height"].get<int>()) {
            continue;
        }
        checkForm(file, bitmap, fixture);
        const int tolerance = fixture["tolerance"];
        checkProbes(file, bitmap, fixture["probes"], tolerance);
        if (fixture.contains("golden")) {
            checkGolden(file, bitmap, fixture["golden"], tolerance);
        }
    }
}

TEST(a_fixture_decodes_reduced_to_the_size_the_rule_gives) {
    for (const nlohmann::json &fixture : manifest().value("fixtures", nlohmann::json::array())) {
        if (fixture.value("invalid", false) || !fixture.contains("scaled") || fixture["scaled"].empty() ||
            !decodable(fixture)) {
            continue;
        }
        const std::string file = fixture["file"];
        for (const nlohmann::json &scaled : fixture["scaled"]) {
            const int maxEdge = scaled["maxEdge"];
            const std::string what = file + " at " + std::to_string(maxEdge);
            const Bitmap bitmap = Bitmap::load(decodeFolder() + file, maxEdge);
            CHECK_DETAIL(bitmap.valid(), what + ": did not decode");
            if (!bitmap.valid()) {
                continue;
            }
            const int width = scaled["width"];
            const int height = scaled["height"];
            CHECK_DETAIL(bitmap.width() == width && bitmap.height() == height,
                         what + ": decoded " + std::to_string(bitmap.width()) + "x" +
                             std::to_string(bitmap.height()) + ", wanted " + std::to_string(width) + "x" +
                             std::to_string(height));
            if (bitmap.width() != width || bitmap.height() != height) {
                continue;
            }
            checkForm(what, bitmap, fixture);
            checkProbes(what, bitmap, scaled["probes"], fixture["tolerance"].get<int>());
        }
    }
}

TEST(a_fixture_decodes_the_same_from_memory_as_from_its_file) {
    for (const nlohmann::json &fixture : manifest().value("fixtures", nlohmann::json::array())) {
        if (fixture.value("invalid", false) || !decodable(fixture)) {
            continue;
        }
        const std::string file = fixture["file"];
        std::vector<uint8_t> bytes;
        CHECK_DETAIL(Bitmap::readFile(decodeFolder() + file, &bytes), file + ": could not be read");
        for (int maxEdge : {0, 48}) {
            const Bitmap fromFile = Bitmap::load(decodeFolder() + file, maxEdge);
            const Bitmap fromMemory = Bitmap::loadFromMemory(bytes.data(), bytes.size(), maxEdge);
            const std::string what = file + " at " + std::to_string(maxEdge);
            CHECK_DETAIL(fromFile.valid() == fromMemory.valid() && fromFile.width() == fromMemory.width() &&
                             fromFile.height() == fromMemory.height(),
                         what + ": file and memory decodes differ in size");
            if (fromFile.valid() && fromFile.width() == fromMemory.width() &&
                fromFile.height() == fromMemory.height()) {
                const size_t size = (size_t)fromFile.width() * (size_t)fromFile.height() * 4;
                CHECK_DETAIL(std::equal(fromFile.pixels(), fromFile.pixels() + size, fromMemory.pixels()),
                             what + ": file and memory decodes differ in pixels");
            }
        }
    }
}

TEST(a_file_that_is_cut_short_or_not_an_image_does_not_decode) {
    for (const nlohmann::json &fixture : manifest().value("fixtures", nlohmann::json::array())) {
        if (!fixture.value("invalid", false)) {
            continue;
        }
        const std::string file = fixture["file"];
        CHECK_DETAIL(!Bitmap::load(decodeFolder() + file, 0).valid(), file + ": decoded whole");
        CHECK_DETAIL(!Bitmap::load(decodeFolder() + file, 48).valid(), file + ": decoded reduced");
        std::vector<uint8_t> bytes;
        Bitmap::readFile(decodeFolder() + file, &bytes);
        CHECK_DETAIL(!Bitmap::loadFromMemory(bytes.data(), bytes.size(), 0).valid(), file + ": decoded from memory");
    }
    CHECK(!Bitmap::loadFromMemory(nullptr, 0, 0).valid());
    CHECK(!Bitmap::load(decodeFolder() + "no_such_file.jpg", 0).valid());
}
