// RegionDecoder on every platform against the decode fixtures. The contract is
// in graphics/RegionDecoder.h: a rectangle inside the image, decoded to the
// same pixels a whole decode has there, at exactly the size asked for.
#include "tests.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "decode_fixtures.h"
#include "graphics/RegionDecoder.h"

using namespace DecodeFixtures;

namespace {

struct Rect {
    int x, y, width, height;
};

std::string describe(const Rect &rect) {
    return std::to_string(rect.width) + "x" + std::to_string(rect.height) + "+" + std::to_string(rect.x) + "+" +
           std::to_string(rect.y);
}

// Whether this platform decodes regions at all. Every platform that does
// decodes them from JPEG.
bool regionsHere() {
    return RegionDecoder::looksSupported("image/jpeg");
}

// The fixture's region decoder. A format this platform has no region decoder
// for is noted; a JPEG with none is a failure.
RegionDecoderPtr openFixture(const nlohmann::json &fixture) {
    const std::string file = fixture["file"];
    RegionDecoderPtr decoder = RegionDecoder::open(folder() + file);
    if (decoder == nullptr && mimeTypeOf(file) != "image/jpeg") {
        reportNote(file + ": no region decoder here");
    }
    return decoder;
}

// The four patches of a quartered fixture, in the manifest's probe order.
std::vector<Rect> quarters(int width, int height) {
    const int w = width / 2;
    const int h = height / 2;
    return {{0, 0, w, h}, {w, 0, width - w, h}, {0, h, w, height - h}, {w, h, width - w, height - h}};
}

// Rectangles that start and end off the 8x8 block grid, one running to the
// right and bottom edges, and the last pixel alone.
std::vector<Rect> oddRectangles(int width, int height) {
    return {{0, 0, width, height},
            {1, 1, width / 3, height / 3},
            {width / 2 - 3, height / 3 + 1, width - (width / 2 - 3), height - (height / 3 + 1)},
            {width - 1, height - 1, 1, 1}};
}

void checkForm(const std::string &what, const Bitmap &tile, const nlohmann::json &fixture) {
    CHECK_DETAIL(tile.order() == Bitmap::decodeOrder(), what + ": pixels not in decodeOrder()");
    if (!fixture["alpha"].get<bool>()) {
        CHECK_DETAIL(tile.knownOpaque(), what + ": a format with no alpha is not marked opaque");
    } else {
        CHECK_DETAIL(!tile.knownOpaque() || !tile.hasTransparency(),
                     what + ": a tile with transparent pixels is marked opaque");
    }
}

}  // namespace

TEST(a_jpeg_fixture_opens_for_regions_at_its_size) {
    if (!regionsHere()) {
        SKIP("no region decoder on this platform");
    }
    for (const nlohmann::json &fixture : all()) {
        const std::string file = fixture["file"];
        if (fixture.value("invalid", false) || mimeTypeOf(file) != "image/jpeg") {
            continue;
        }
        const RegionDecoderPtr decoder = RegionDecoder::open(folder() + file);
        CHECK_DETAIL(decoder != nullptr, file + ": no region decoder");
        if (decoder != nullptr) {
            CHECK_DETAIL(decoder->width() == fixture["width"].get<int>() &&
                             decoder->height() == fixture["height"].get<int>(),
                         file + ": opened at " + std::to_string(decoder->width()) + "x" +
                             std::to_string(decoder->height()));
        }
    }
}

TEST(a_format_that_opens_for_regions_is_offered_for_them) {
    // looksSupported is what a data source asks every frame. A format that
    // opens but is not offered is never tiled.
    for (const nlohmann::json &fixture : all()) {
        const std::string file = fixture["file"];
        if (fixture.value("invalid", false) || RegionDecoder::open(folder() + file) == nullptr) {
            continue;
        }
        CHECK_DETAIL(RegionDecoder::looksSupported(mimeTypeOf(file)),
                     file + ": opens, but " + mimeTypeOf(file) + " is not offered");
    }
}

TEST(a_region_at_full_size_has_the_fixture_pixels) {
    if (!regionsHere()) {
        SKIP("no region decoder on this platform");
    }
    for (const nlohmann::json &fixture : all()) {
        if (fixture.value("invalid", false)) {
            continue;
        }
        const RegionDecoderPtr decoder = openFixture(fixture);
        if (decoder == nullptr) {
            continue;
        }
        const std::string file = fixture["file"];
        const int width = fixture["width"];
        const int height = fixture["height"];
        const int tolerance = fixture["tolerance"];

        std::vector<Rect> rectangles = quarters(width, height);
        if (fixture.contains("golden")) {
            const std::vector<Rect> odd = oddRectangles(width, height);
            rectangles.insert(rectangles.end(), odd.begin(), odd.end());
        }
        for (const Rect &rect : rectangles) {
            if (rect.width <= 0 || rect.height <= 0) {
                continue;
            }
            const std::string what = file + " " + describe(rect);
            const Bitmap tile = decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, rect.width, rect.height);
            CHECK_DETAIL(tile.valid(), what + ": did not decode");
            if (!tile.valid()) {
                continue;
            }
            checkForm(what, tile, fixture);
            // The manifest's probes that fall inside, moved to the tile's
            // corner.
            nlohmann::json inside = nlohmann::json::array();
            for (const nlohmann::json &probe : fixture["probes"]) {
                const int x = probe[0];
                const int y = probe[1];
                if (x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height) {
                    inside.push_back({x - rect.x, y - rect.y, probe[2], probe[3], probe[4], probe[5]});
                }
            }
            checkProbes(what, tile, inside, tolerance);
            if (fixture.contains("golden")) {
                checkGolden(what, tile, fixture["golden"], width, rect.x, rect.y, tolerance);
            }
        }
    }
}

TEST(a_reduced_region_has_the_fixture_colours_at_the_size_asked_for) {
    if (!regionsHere()) {
        SKIP("no region decoder on this platform");
    }
    for (const nlohmann::json &fixture : all()) {
        if (fixture.value("invalid", false) || fixture.value("pattern", "") != "quarters") {
            continue;
        }
        const RegionDecoderPtr decoder = openFixture(fixture);
        if (decoder == nullptr) {
            continue;
        }
        const std::string file = fixture["file"];
        const int tolerance = fixture["tolerance"];
        const std::vector<Rect> rectangles = quarters(fixture["width"], fixture["height"]);
        for (int sample : {2, 4, 8, 16}) {
            for (size_t index = 0; index < rectangles.size(); ++index) {
                const Rect &rect = rectangles[index];
                // Two output pixels each way at least, so the middle is inside
                // the patch.
                if (rect.width < sample * 2 || rect.height < sample * 2) {
                    continue;
                }
                const int outWidth = rect.width / sample;
                const int outHeight = rect.height / sample;
                const std::string what = file + " " + describe(rect) + " at 1/" + std::to_string(sample);
                const Bitmap tile = decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, outWidth, outHeight);
                CHECK_DETAIL(tile.valid(), what + ": did not decode");
                if (!tile.valid()) {
                    continue;
                }
                CHECK_DETAIL(tile.width() == outWidth && tile.height() == outHeight,
                             what + ": decoded " + std::to_string(tile.width()) + "x" +
                                 std::to_string(tile.height()));
                checkForm(what, tile, fixture);
                const nlohmann::json &probe = fixture["probes"][index];
                const nlohmann::json middle = nlohmann::json::array(
                    {nlohmann::json::array({outWidth / 2, outHeight / 2, probe[2], probe[3], probe[4], probe[5]})});
                checkProbes(what, tile, middle, tolerance);
            }
        }
    }
}

TEST(a_reduced_region_lands_on_its_part_of_the_picture) {
    // The ramps change eight levels a pixel, so a reduced tile taken from a
    // little off its rectangle is further from the golden averaged over that
    // rectangle than a scaler's rounding.
    if (!regionsHere()) {
        SKIP("no region decoder on this platform");
    }
    for (const nlohmann::json &fixture : all()) {
        if (fixture.value("invalid", false) || fixture.value("pattern", "") != "ramps") {
            continue;
        }
        const RegionDecoderPtr decoder = openFixture(fixture);
        if (decoder == nullptr) {
            continue;
        }
        const std::string file = fixture["file"];
        const int width = fixture["width"];
        const int height = fixture["height"];
        std::vector<uint8_t> golden;
        CHECK(Bitmap::readFile(folder() + fixture["golden"].get<std::string>(), &golden));
        if (golden.size() != (size_t)width * (size_t)height * 4) {
            continue;
        }
        for (int sample : {2, 4, 8}) {
            // From a multiple of the reduction, as the tile grid asks, and as
            // many whole samples as fit.
            const Rect rect = {sample, sample * 2, (width - sample) / sample * sample,
                               (height - sample * 2) / sample * sample};
            const int outWidth = rect.width / sample;
            const int outHeight = rect.height / sample;
            const std::string what = file + " " + describe(rect) + " at 1/" + std::to_string(sample);
            const Bitmap tile = decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, outWidth, outHeight);
            CHECK_DETAIL(tile.valid() && tile.width() == outWidth && tile.height() == outHeight,
                         what + ": did not decode at the size asked for");
            if (!tile.valid() || tile.width() != outWidth || tile.height() != outHeight) {
                continue;
            }
            const Difference difference =
                fromGoldenAverage(tile, golden, width, rect.x, rect.y, rect.width, rect.height);
            CHECK_DETAIL(difference.mean <= 6, what + ": off the golden by " + std::to_string(difference.mean) +
                                                   " on average, " + std::to_string(difference.worst) + " at worst");
        }
    }
}

TEST(a_region_not_inside_the_image_is_invalid) {
    if (!regionsHere()) {
        SKIP("no region decoder on this platform");
    }
    const RegionDecoderPtr decoder = RegionDecoder::open(folder() + "baseline.jpg");
    CHECK(decoder != nullptr);
    if (decoder == nullptr) {
        return;
    }
    CHECK_EQ(decoder->width(), 96);
    CHECK_EQ(decoder->height(), 64);
    // Up to the edges is inside.
    CHECK(decoder->decodeRegion(0, 0, 96, 64, 96, 64).valid());
    CHECK(decoder->decodeRegion(95, 63, 1, 1, 1, 1).valid());

    struct Request {
        int x, y, width, height, outWidth, outHeight;
    };
    const Request outside[] = {
        {-1, 0, 10, 10, 10, 10},      {0, -1, 10, 10, 10, 10},     {87, 0, 10, 10, 10, 10},
        {0, 55, 10, 10, 10, 10},      {96, 0, 1, 1, 1, 1},         {0, 64, 1, 1, 1, 1},
        {0, 0, 97, 64, 97, 64},       {0, 0, 96, 65, 96, 65},      {0, 0, 0, 10, 1, 10},
        {0, 0, 10, 0, 10, 1},         {0, 0, 10, -5, 10, 5},       {0, 0, 10, 10, 0, 10},
        {0, 0, 10, 10, 10, 0},        {0, 0, 10, 10, -10, 10},     {INT_MAX, 0, 1, 1, 1, 1},
        {1, 0, INT_MAX, 1, 1, 1},     {0, 1, 1, INT_MAX, 1, 1},    {INT_MIN, INT_MIN, 10, 10, 10, 10},
    };
    for (const Request &r : outside) {
        const Bitmap tile = decoder->decodeRegion(r.x, r.y, r.width, r.height, r.outWidth, r.outHeight);
        CHECK_DETAIL(!tile.valid(), "decoded " + describe(Rect{r.x, r.y, r.width, r.height}) + " to " +
                                        std::to_string(r.outWidth) + "x" + std::to_string(r.outHeight));
    }
}

TEST(a_region_of_a_file_that_is_cut_short_or_not_an_image_is_invalid) {
    for (const nlohmann::json &fixture : all()) {
        if (!fixture.value("invalid", false)) {
            continue;
        }
        const std::string file = fixture["file"];
        const RegionDecoderPtr decoder = RegionDecoder::open(folder() + file);
        if (decoder == nullptr) {
            continue;
        }
        // The header can be whole when the scan is not. A tile of what is
        // missing still does not decode.
        for (int sample : {1, 8}) {
            const int width = decoder->width();
            const int height = decoder->height();
            const Bitmap tile = decoder->decodeRegion(0, 0, width, height, std::max(1, width / sample),
                                                      std::max(1, height / sample));
            CHECK_DETAIL(!tile.valid(), file + ": decoded whole at 1/" + std::to_string(sample));
        }
    }
    CHECK(RegionDecoder::open(folder() + "no_such_file.jpg") == nullptr);
}

TEST(one_region_decoder_serves_eight_threads_at_once) {
    if (!regionsHere()) {
        SKIP("no region decoder on this platform");
    }
    const RegionDecoderPtr decoder = RegionDecoder::open(folder() + "gradient.jpg");
    CHECK(decoder != nullptr);
    if (decoder == nullptr) {
        return;
    }
    // A grid of tiles at full size and halved, each decoded once alone first.
    struct Tile {
        Rect rect;
        int sample;
        Bitmap expected;
    };
    std::vector<Tile> tiles;
    for (int sample : {1, 2}) {
        const int edge = 48;
        for (int y = 0; y + edge <= decoder->height(); y += edge) {
            for (int x = 0; x + edge <= decoder->width(); x += edge) {
                const Rect rect = {x, y, edge, edge};
                tiles.push_back({rect, sample, decoder->decodeRegion(x, y, edge, edge, edge / sample, edge / sample)});
            }
        }
    }
    std::atomic<int> differing{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 8; ++thread) {
        threads.emplace_back([&, thread]() {
            for (int round = 0; round < 3; ++round) {
                for (size_t i = 0; i < tiles.size(); ++i) {
                    const Tile &tile = tiles[(i + (size_t)thread * 7) % tiles.size()];
                    const Rect &r = tile.rect;
                    const Bitmap got =
                        decoder->decodeRegion(r.x, r.y, r.width, r.height, r.width / tile.sample, r.height / tile.sample);
                    const bool same = got.valid() && tile.expected.valid() && got.width() == tile.expected.width() &&
                                      got.height() == tile.expected.height() &&
                                      std::equal(got.pixels(),
                                                 got.pixels() + (size_t)got.width() * (size_t)got.height() * 4,
                                                 tile.expected.pixels());
                    if (!same) {
                        ++differing;
                    }
                }
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }
    CHECK_EQ(differing.load(), 0);
}
