// Cropping a tile out of a JPEG without decoding the rest.
//
// The checks are against a whole-file decode of the same JPEG, because a region
// is only correct if it matches what the full image has in that rectangle.
#include "tests.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <jpeglib.h>

#include "Bitmap.h"
#include "LocalDataSource.h"
#include "MediaItem.h"
#include "MediaSet.h"
#include "RegionDecoder.h"
#include "TiledImage.h"

namespace fs = std::filesystem;

namespace {

// A JPEG whose colour varies with position, so a tile taken from the wrong
// offset does not accidentally match the right one. Quality 100 with sampling
// off keeps the codec from blurring neighbouring pixels together, which is what
// lets the comparison below be exact.
std::string writeGradientJpeg(const char *name, int width, int height) {
    const fs::path path = fs::temp_directory_path() / name;
    std::FILE *file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return std::string();
    }

    jpeg_compress_struct cinfo {};
    jpeg_error_mgr error {};
    cinfo.err = jpeg_std_error(&error);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, file);
    cinfo.image_width = (unsigned)width;
    cinfo.image_height = (unsigned)height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 100, TRUE);
    cinfo.comp_info[0].h_samp_factor = 1;
    cinfo.comp_info[0].v_samp_factor = 1;
    jpeg_start_compress(&cinfo, TRUE);

    std::vector<uint8_t> row((size_t)width * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            row[(size_t)x * 3 + 0] = (uint8_t)(x % 256);
            row[(size_t)x * 3 + 1] = (uint8_t)(y % 256);
            row[(size_t)x * 3 + 2] = (uint8_t)((x + y) % 256);
        }
        JSAMPROW rows[1] = {row.data()};
        jpeg_write_scanlines(&cinfo, rows, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    std::fclose(file);
    return path.string();
}

// The largest per-channel difference between a region and the same rectangle of
// the whole image.
int worstChannelDifference(const Bitmap &region, const Bitmap &whole, int originX, int originY) {
    int worst = 0;
    for (int y = 0; y < region.height(); ++y) {
        for (int x = 0; x < region.width(); ++x) {
            const uint8_t *tile = region.pixels() + ((size_t)y * (size_t)region.width() + (size_t)x) * 4;
            const uint8_t *full =
                whole.pixels() +
                ((size_t)(y + originY) * (size_t)whole.width() + (size_t)(x + originX)) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                const int difference = (int)tile[channel] - (int)full[channel];
                worst = std::max(worst, difference < 0 ? -difference : difference);
            }
        }
    }
    return worst;
}

}  // namespace

TEST(a_region_matches_the_same_rectangle_of_the_whole_image) {
    const std::string path = writeGradientJpeg("gallery3d_region.jpg", 800, 600);
    CHECK(!path.empty());
    const Bitmap whole = Bitmap::load(path, 0);
    CHECK(whole.valid());
    CHECK_EQ(whole.width(), 800);

    // x = 101 and y = 37 sit inside an MCU, so libjpeg hands back a window
    // starting left of the one asked for. Getting that inset wrong shifts the
    // tile, which is what this catches.
    struct Rect {
        int x, y, width, height;
    };
    const Rect rectangles[] = {
        {0, 0, 256, 256},     // the origin, already on a boundary
        {101, 37, 200, 150},  // off boundary in both directions
        {512, 320, 288, 280}, // runs to the right and bottom edges
        {799, 599, 1, 1},     // the last pixel on its own
    };

    const RegionDecoderPtr decoder = RegionDecoder::open(path);
    CHECK(decoder != nullptr);
    if (decoder == nullptr) {
        return;
    }
    CHECK_EQ(decoder->width(), 800);
    CHECK_EQ(decoder->height(), 600);

    for (const Rect &rect : rectangles) {
        const Bitmap tile =
            decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, rect.width, rect.height);
        CHECK(tile.valid());
        CHECK_EQ(tile.width(), rect.width);
        CHECK_EQ(tile.height(), rect.height);
        if (tile.valid() && tile.width() == rect.width && tile.height() == rect.height) {
            CHECK_EQ(worstChannelDifference(tile, whole, rect.x, rect.y), 0);
        }
    }

    fs::remove(path);
}

TEST(a_sampled_region_comes_back_at_the_size_asked_for) {
    const std::string path = writeGradientJpeg("gallery3d_region_sampled.jpg", 800, 600);
    CHECK(!path.empty());

    // The tile grid asks for whole-number reductions. Two and four are
    // libjpeg's own; sixteen is past the eighth it can do, so the decoder has
    // to finish the reduction itself.
    const RegionDecoderPtr decoder = RegionDecoder::open(path);
    CHECK(decoder != nullptr);
    if (decoder == nullptr) {
        return;
    }
    const int sampleSizes[] = {2, 4, 16};
    for (int sampleSize : sampleSizes) {
        const int width = 512;
        const int height = 256;
        const Bitmap tile = decoder->decodeRegion(128, 64, width, height, width / sampleSize, height / sampleSize);
        CHECK(tile.valid());
        CHECK_EQ(tile.width(), width / sampleSize);
        CHECK_EQ(tile.height(), height / sampleSize);
    }

    fs::remove(path);
}

TEST(one_decoder_serves_every_tile_and_outlives_the_file) {
    // Opening reads the file; decoding does not go back to it. A zoom asks for
    // dozens of regions, so reopening each time would read the same photo
    // dozens of times over.
    const std::string path = writeGradientJpeg("gallery3d_region_reuse.jpg", 1024, 768);
    const RegionDecoderPtr decoder = RegionDecoder::open(path);
    CHECK(decoder != nullptr);
    if (decoder == nullptr) {
        return;
    }

    // Deleting the file is how this says the decoder is not reaching for it
    // again. Every region below comes from what open() already read.
    fs::remove(path);
    CHECK(!fs::exists(path));

    for (int column = 0; column < 4; ++column) {
        const Bitmap tile = decoder->decodeRegion(column * 256, 0, 256, 256, 256, 256);
        CHECK(tile.valid());
        CHECK_EQ(tile.width(), 256);
        // Red follows x in the gradient, so each tile starts where the last
        // one ended.
        if (tile.valid()) {
            CHECK_NEAR(tile.pixels()[0], (column * 256) % 256, 2);
        }
    }
}

TEST(a_sampled_region_still_lands_on_the_right_part_of_the_picture) {
    const std::string path = writeGradientJpeg("gallery3d_region_placed.jpg", 800, 600);
    CHECK(!path.empty());

    // Red follows x and green follows y in the gradient, so a tile's corner
    // colour says where it was taken from. Halving loses a little to the
    // resampler, hence the tolerance rather than an exact value.
    const RegionDecoderPtr decoder = RegionDecoder::open(path);
    CHECK(decoder != nullptr);
    if (decoder == nullptr) {
        return;
    }
    const Bitmap tile = decoder->decodeRegion(400, 200, 256, 256, 128, 128);
    CHECK(tile.valid());
    if (tile.valid()) {
        const uint8_t *topLeft = tile.pixels();
        CHECK_NEAR(topLeft[0], 400 % 256, 6);
        CHECK_NEAR(topLeft[1], 200 % 256, 6);
    }

    fs::remove(path);
}

TEST(only_a_jpeg_offers_regions) {
    // The cheap check the draw path makes every frame, which must not open
    // anything.
    CHECK(RegionDecoder::looksSupported("image/jpeg"));
    CHECK(!RegionDecoder::looksSupported("image/png"));
    CHECK(!RegionDecoder::looksSupported("video/mp4"));
    CHECK(!RegionDecoder::looksSupported(""));

    const std::string path = writeGradientJpeg("gallery3d_region_probe.jpg", 64, 64);
    CHECK(RegionDecoder::open(path) != nullptr);
    fs::remove(path);

    // The wall's own art is PNG, which has no region decoder behind it.
    CHECK(RegionDecoder::open(std::string(GALLERY3D_ASSET_ROOT) + "/drawable/icon_home_small.png") == nullptr);
    CHECK(RegionDecoder::open("/no/such/file.jpg") == nullptr);
}

TEST(a_region_outside_the_picture_fails_instead_of_guessing) {
    const std::string path = writeGradientJpeg("gallery3d_region_outside.jpg", 128, 128);
    const RegionDecoderPtr decoder = RegionDecoder::open(path);
    CHECK(decoder != nullptr);
    if (decoder != nullptr) {
        CHECK(!decoder->decodeRegion(200, 200, 64, 64, 64, 64).valid());
        CHECK(!decoder->decodeRegion(0, 0, 0, 0, 0, 0).valid());
    }
    fs::remove(path);
}

TEST(the_cache_holds_one_decoder_and_swaps_it_for_another_photo) {
    // What stops a zoom reopening the same photo once per tile, and what lets
    // the next photo replace it.
    const std::string first = writeGradientJpeg("gallery3d_region_cache_a.jpg", 512, 512);
    const std::string second = writeGradientJpeg("gallery3d_region_cache_b.jpg", 256, 256);

    RegionDecoderCache cache;
    const RegionDecoderPtr a = cache.get(first);
    CHECK(a != nullptr);
    // The same source gets the same decoder back rather than a second one.
    CHECK(cache.get(first) == a);

    const RegionDecoderPtr b = cache.get(second);
    CHECK(b != nullptr);
    CHECK(b != a);
    if (b != nullptr) {
        CHECK_EQ(b->width(), 256);
    }

    // The first decoder is still usable while someone holds it, even though the
    // cache has moved on. In the app that someone is a tile still decoding.
    fs::remove(first);
    CHECK(a->decodeRegion(0, 0, 256, 256, 256, 256).valid());

    // A source with no decoder behind it caches the miss rather than reopening.
    CHECK(cache.get("/no/such/file.jpg") == nullptr);

    fs::remove(second);
}

TEST(the_header_pass_reports_the_pixel_size) {
    // Tiling needs the full size before anything is decoded, and the header
    // pass is where the scan already is.
    const std::string path = writeGradientJpeg("gallery3d_region_size.jpg", 640, 480);
    const Bitmap::ExifInfo info = Bitmap::readExif(path);
    CHECK_EQ(info.pixelWidth, 640);
    CHECK_EQ(info.pixelHeight, 480);
    fs::remove(path);

    // A PNG has no start-of-frame marker, so it reports nothing rather than
    // something wrong.
    const Bitmap::ExifInfo none =
        Bitmap::readExif(std::string(GALLERY3D_ASSET_ROOT) + "/drawable/icon_home_small.png");
    CHECK_EQ(none.pixelWidth, 0);
    CHECK_EQ(none.pixelHeight, 0);
}

TEST(a_local_jpeg_is_tileable_and_a_png_is_not) {
    // The wiring the fullscreen view checks before it asks for any tile: a
    // source that crops, and an item that knows its own size.
    const std::string jpeg = writeGradientJpeg("gallery3d_region_cantile.jpg", 4000, 3000);
    LocalDataSource source(fs::temp_directory_path().string());
    MediaSet set;
    set.mDataSource = &source;

    MediaItem photo;
    photo.mParentMediaSet = &set;
    photo.mFilePath = jpeg;
    photo.mMimeType = "image/jpeg";
    const Bitmap::ExifInfo info = Bitmap::readExif(jpeg);
    photo.mFullWidth = info.pixelWidth;
    photo.mFullHeight = info.pixelHeight;
    CHECK(TiledImage::canTile(&photo));

    // Same source, but nothing behind it can crop a PNG.
    MediaItem drawing;
    drawing.mParentMediaSet = &set;
    drawing.mFilePath = std::string(GALLERY3D_ASSET_ROOT) + "/drawable/icon_home_small.png";
    drawing.mMimeType = "image/png";
    drawing.mFullWidth = 64;
    drawing.mFullHeight = 64;
    CHECK(!TiledImage::canTile(&drawing));

    fs::remove(jpeg);
}

TEST(a_local_tile_comes_back_through_the_data_source) {
    const std::string jpeg = writeGradientJpeg("gallery3d_region_source.jpg", 2000, 1500);
    LocalDataSource source(fs::temp_directory_path().string());
    MediaItem photo;
    photo.mFilePath = jpeg;
    photo.mMimeType = "image/jpeg";
    photo.mFullWidth = 2000;
    photo.mFullHeight = 1500;

    Bitmap tile;
    source.requestRegion(&photo, 512, 512, 512, 512, 512, 512,
                         [&tile](Bitmap bitmap) { tile = std::move(bitmap); });
    CHECK(tile.valid());
    CHECK_EQ(tile.width(), 512);

    // A rectangle running off the right edge is trimmed, not refused.
    Bitmap edge;
    source.requestRegion(&photo, 1900, 1400, 512, 512, 100, 100,
                         [&edge](Bitmap bitmap) { edge = std::move(bitmap); });
    CHECK(edge.valid());

    // The source holds the decoder open, so the tiles after the first one do
    // not go back to the disk.
    fs::remove(jpeg);
    Bitmap afterDelete;
    source.requestRegion(&photo, 0, 0, 512, 512, 512, 512,
                         [&afterDelete](Bitmap bitmap) { afterDelete = std::move(bitmap); });
    CHECK(afterDelete.valid());

    // A different photo replaces it, and that one is gone.
    MediaItem other;
    other.mFilePath = jpeg + ".other";
    other.mMimeType = "image/jpeg";
    other.mFullWidth = 100;
    other.mFullHeight = 100;
    Bitmap missing;
    source.requestRegion(&other, 0, 0, 50, 50, 50, 50,
                         [&missing](Bitmap bitmap) { missing = std::move(bitmap); });
    CHECK(!missing.valid());
}
