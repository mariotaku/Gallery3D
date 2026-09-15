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
// - maxEdge picks a power of two sample, the largest whose reduced long edge
//   still reaches maxEdge, and the picture comes back at the size and with
//   the pixels Android's decoder gives for that sample (Sampling in Bitmap.h).
//   maxEdge 0 or below keeps the size.
// - A reduced decode may answer from an embedded thumbnail in the picture's
//   shape whose long edge reaches maxEdge, decoded for maxEdge in turn.
// - A file that is cut short or is not an image does not decode.
#include "tests.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "decode_fixtures.h"
#include "graphics/Bitmap.h"

using namespace DecodeFixtures;

namespace {

// Whether this platform decodes the fixture's format at all. A format it has
// no decoder for is noted rather than failed; which formats every platform must
// decode is checked on its own. Lossless WebP is noted the same way where the
// WebP codec reads lossy files only.
bool decodable(const nlohmann::json &fixture) {
    const std::string file = fixture["file"];
    if (!Bitmap::decodesExtension(extensionOf(file))) {
        reportNote(file + ": no decoder for " + extensionOf(file) + " here");
        return false;
    }
    if (extensionOf(file) == ".webp" && fixture.value("lossless", false) && !Bitmap::decodesLosslessWebp()) {
        reportNote(file + ": the WebP codec here reads no lossless WebP");
        return false;
    }
    return true;
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
                 "no manifest at " + folder() + "manifest.json");
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
    for (const nlohmann::json &fixture : all()) {
        if (fixture.value("invalid", false) || !decodable(fixture)) {
            continue;
        }
        const std::string file = fixture["file"];
        const Bitmap bitmap = Bitmap::load(folder() + file, 0);
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
            checkGolden(file, bitmap, fixture["golden"], bitmap.width(), 0, 0, tolerance);
        }
    }
}

TEST(a_fixture_decodes_reduced_to_the_size_the_rule_gives) {
    for (const nlohmann::json &fixture : all()) {
        if (fixture.value("invalid", false) || !fixture.contains("scaled") || fixture["scaled"].empty() ||
            !decodable(fixture)) {
            continue;
        }
        const std::string file = fixture["file"];
        for (const nlohmann::json &scaled : fixture["scaled"]) {
            const int maxEdge = scaled["maxEdge"];
            const std::string what = file + " at " + std::to_string(maxEdge);
            const Bitmap bitmap = Bitmap::load(folder() + file, maxEdge);
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
            // Ramps change every pixel, so a golden of the reduced picture
            // shows where each reduced pixel came from.
            if (scaled.contains("golden")) {
                checkGolden(what, bitmap, scaled["golden"], width, 0, 0, fixture["tolerance"].get<int>());
            }
        }
    }
}

TEST(a_fixture_decodes_the_same_from_memory_as_from_its_file) {
    for (const nlohmann::json &fixture : all()) {
        if (fixture.value("invalid", false) || !decodable(fixture)) {
            continue;
        }
        const std::string file = fixture["file"];
        std::vector<uint8_t> bytes;
        CHECK_DETAIL(Bitmap::readFile(folder() + file, &bytes), file + ": could not be read");
        for (int maxEdge : {0, 48}) {
            const Bitmap fromFile = Bitmap::load(folder() + file, maxEdge);
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
    for (const nlohmann::json &fixture : all()) {
        if (!fixture.value("invalid", false)) {
            continue;
        }
        const std::string file = fixture["file"];
        CHECK_DETAIL(!Bitmap::load(folder() + file, 0).valid(), file + ": decoded whole");
        CHECK_DETAIL(!Bitmap::load(folder() + file, 48).valid(), file + ": decoded reduced");
        std::vector<uint8_t> bytes;
        Bitmap::readFile(folder() + file, &bytes);
        CHECK_DETAIL(!Bitmap::loadFromMemory(bytes.data(), bytes.size(), 0).valid(), file + ": decoded from memory");
    }
    CHECK(!Bitmap::loadFromMemory(nullptr, 0, 0).valid());
    CHECK(!Bitmap::load(folder() + "no_such_file.jpg", 0).valid());
}
