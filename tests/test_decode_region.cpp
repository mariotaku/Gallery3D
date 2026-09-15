// RegionDecoder on every platform against the decode fixtures. The contract is
// in graphics/RegionDecoder.h: a rectangle inside the image, reduced by a power
// of two sample to the size and pixels Android's BitmapRegionDecoder gives.
#include "tests.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
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
// for is noted; a JPEG with none is a failure elsewhere.
RegionDecoderPtr openFixture(const nlohmann::json &fixture) {
    const std::string file = fixture["file"];
    RegionDecoderPtr decoder = RegionDecoder::open(folder() + file);
    if (decoder == nullptr && mimeTypeOf(file) != "image/jpeg") {
        reportNote(file + ": no region decoder here");
    }
    return decoder;
}

Sampling samplingOfFile(const std::string &file) {
    std::vector<uint8_t> bytes;
    Bitmap::readFile(folder() + file, &bytes);
    return Bitmap::samplingOf(bytes.data(), bytes.size());
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
            // A WebP rectangle is widened to start at even coordinates, which
            // the odd ones here would not survive.
            if (rect.width <= 0 || rect.height <= 0 ||
                (samplingOfFile(file) == Sampling::Rescaled && ((rect.x | rect.y) & 1) != 0)) {
                continue;
            }
            const std::string what = file + " " + describe(rect);
            const Bitmap tile = decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, 1);
            CHECK_DETAIL(tile.valid() && tile.width() == rect.width && tile.height() == rect.height,
                         what + ": did not decode at its own size");
            if (!tile.valid() || tile.width() != rect.width || tile.height() != rect.height) {
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

TEST(a_reduced_region_comes_back_at_the_size_android_gives) {
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
        const int width = fixture["width"];
        const int height = fixture["height"];
        const int tolerance = fixture["tolerance"];
        const Sampling sampling = samplingOfFile(file);
        const std::vector<Rect> rectangles = quarters(width, height);
        for (int sample : {2, 4, 8, 16}) {
            // The whole picture, at the size a whole decode has.
            const Bitmap::Size wholeSize = Bitmap::sampledSize(sampling, width, height, sample);
            const Bitmap whole = decoder->decodeRegion(0, 0, width, height, sample);
            const std::string wholeWhat = file + " whole at 1/" + std::to_string(sample);
            CHECK_DETAIL(whole.valid() && whole.width() == wholeSize.width && whole.height() == wholeSize.height,
                         wholeWhat + ": decoded " + std::to_string(whole.width()) + "x" +
                             std::to_string(whole.height()) + ", wanted " + std::to_string(wholeSize.width) + "x" +
                             std::to_string(wholeSize.height));
            if (whole.valid()) {
                checkForm(wholeWhat, whole, fixture);
            }

            for (size_t index = 0; index < rectangles.size(); ++index) {
                const Rect &rect = rectangles[index];
                // Two reduced pixels each way at least, so the middle is
                // inside the patch.
                if (rect.width < sample * 2 || rect.height < sample * 2 ||
                    (sampling == Sampling::Rescaled && ((rect.x | rect.y) & 1) != 0)) {
                    continue;
                }
                const Bitmap::Size size = Bitmap::sampledRegionSize(rect.width, rect.height, sample);
                const std::string what = file + " " + describe(rect) + " at 1/" + std::to_string(sample);
                const Bitmap tile = decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, sample);
                CHECK_DETAIL(tile.valid() && tile.width() == size.width && tile.height() == size.height,
                             what + ": decoded " + std::to_string(tile.width()) + "x" +
                                 std::to_string(tile.height()) + ", wanted " + std::to_string(size.width) + "x" +
                                 std::to_string(size.height));
                if (!tile.valid() || tile.width() != size.width || tile.height() != size.height) {
                    continue;
                }
                checkForm(what, tile, fixture);
                const nlohmann::json &probe = fixture["probes"][index];
                const nlohmann::json middle = nlohmann::json::array(
                    {nlohmann::json::array({size.width / 2, size.height / 2, probe[2], probe[3], probe[4], probe[5]})});
                checkProbes(what, tile, middle, tolerance);
            }
        }
    }
}

TEST(a_reduced_region_has_the_pixels_of_the_reduced_decode_there) {
    // The ramps change every pixel. A tile whose corner is a multiple of the
    // sample holds the reduced decode's pixels from its corner divided by the
    // sample, so it matches that golden cut at the same place.
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
        const int tolerance = fixture["tolerance"];
        for (const nlohmann::json &scaled : fixture["scaled"]) {
            if (!scaled.contains("golden")) {
                continue;
            }
            const int sample = scaled["sampleSize"];
            const int goldenWidth = scaled["width"];
            const std::vector<Rect> rectangles = {
                {0, 0, width, height},
                {sample * 2, sample * 3, sample * 8, sample * 6},
                {sample, sample, sample * 4, sample * 4},
            };
            for (const Rect &rect : rectangles) {
                if (rect.x + rect.width > width || rect.y + rect.height > height) {
                    continue;
                }
                const std::string what = file + " " + describe(rect) + " at 1/" + std::to_string(sample);
                const Bitmap tile = decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, sample);
                CHECK_DETAIL(tile.valid(), what + ": did not decode");
                if (tile.valid()) {
                    checkGolden(what, tile, scaled["golden"], goldenWidth, rect.x / sample, rect.y / sample,
                                tolerance);
                }
            }
        }
    }
}

TEST(a_region_not_inside_the_image_or_at_no_power_of_two_is_invalid) {
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
    CHECK(decoder->decodeRegion(0, 0, 96, 64, 1).valid());
    CHECK(decoder->decodeRegion(95, 63, 1, 1, 1).valid());
    // A sample larger than the rectangle still gives a pixel.
    const Bitmap sliver = decoder->decodeRegion(90, 0, 6, 64, 8);
    CHECK(sliver.valid());
    CHECK_EQ(sliver.width(), 1);
    CHECK_EQ(sliver.height(), 8);

    struct Request {
        int x, y, width, height, sampleSize;
    };
    const Request outside[] = {
        {-1, 0, 10, 10, 1},      {0, -1, 10, 10, 1},        {87, 0, 10, 10, 1},     {0, 55, 10, 10, 1},
        {96, 0, 1, 1, 1},        {0, 64, 1, 1, 1},          {0, 0, 97, 64, 1},      {0, 0, 96, 65, 1},
        {0, 0, 0, 10, 1},        {0, 0, 10, 0, 1},          {0, 0, 10, -5, 1},      {0, 0, 10, 10, 0},
        {0, 0, 10, 10, -2},      {0, 0, 10, 10, 3},         {0, 0, 10, 10, 6},      {INT_MAX, 0, 1, 1, 1},
        {1, 0, INT_MAX, 1, 1},   {0, 1, 1, INT_MAX, 1},     {INT_MIN, INT_MIN, 10, 10, 1},
    };
    for (const Request &r : outside) {
        const Bitmap tile = decoder->decodeRegion(r.x, r.y, r.width, r.height, r.sampleSize);
        CHECK_DETAIL(!tile.valid(), "decoded " + describe(Rect{r.x, r.y, r.width, r.height}) + " at sample " +
                                        std::to_string(r.sampleSize));
    }
}

TEST(a_webp_region_starts_at_even_coordinates) {
    const RegionDecoderPtr decoder = RegionDecoder::open(folder() + "lossless.webp");
    if (decoder == nullptr) {
        SKIP("no WebP region decoder on this platform");
    }
    // libwebp decodes from even coordinates, and the rectangle is widened back
    // to them rather than shifted.
    const Bitmap tile = decoder->decodeRegion(1, 3, 10, 10, 1);
    CHECK(tile.valid());
    CHECK_EQ(tile.width(), 11);
    CHECK_EQ(tile.height(), 11);
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
            const Bitmap tile = decoder->decodeRegion(0, 0, decoder->width(), decoder->height(), sample);
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
                tiles.push_back({{x, y, edge, edge}, sample, decoder->decodeRegion(x, y, edge, edge, sample)});
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
                    const Bitmap got = decoder->decodeRegion(r.x, r.y, r.width, r.height, tile.sample);
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
