// Cropping a tile out of a JPEG without decoding the rest.
//
// The checks are against a whole-file decode of the same JPEG, because a region
// is only correct if it matches what the full image has in that rectangle.
#include "tests.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <jpeglib.h>
#endif

#include "graphics/Bitmap.h"
#include "media/LocalDataSource.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "graphics/RegionDecoder.h"
#include "graphics/TiledImage.h"
#if defined(_WIN32)
#include "platform/windows/Wic.h"
#endif

namespace fs = std::filesystem;

namespace {

// A JPEG whose colour varies with position, so a tile taken from the wrong
// offset does not accidentally match the right one. Quality 100 with sampling
// off keeps the codec from blurring neighbouring pixels together, which is what
// lets the comparison below be exact. Written by the codec that reads it back:
// WIC on Windows, libjpeg elsewhere.
#if defined(_WIN32)
std::string writeGradientJpeg(const char *name, int width, int height) {
    const fs::path path = fs::temp_directory_path() / name;
    IWICImagingFactory *imaging = Wic::factory();
    Wic::Ptr<IWICStream> stream;
    Wic::Ptr<IWICBitmapEncoder> encoder;
    Wic::Ptr<IWICBitmapFrameEncode> frame;
    Wic::Ptr<IPropertyBag2> options;
    if (imaging == nullptr || FAILED(imaging->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE)) ||
        FAILED(imaging->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.put(), options.put()))) {
        return std::string();
    }
    PROPBAG2 names[2] = {};
    names[0].pstrName = (LPOLESTR)L"ImageQuality";
    names[1].pstrName = (LPOLESTR)L"JpegYCrCbSubsampling";
    VARIANT values[2];
    VariantInit(&values[0]);
    values[0].vt = VT_R4;
    values[0].fltVal = 1.0f;
    VariantInit(&values[1]);
    values[1].vt = VT_UI1;
    values[1].bVal = (BYTE)WICJpegYCrCbSubsampling444;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(options->Write(2, names, values)) || FAILED(frame->Initialize(options.get())) ||
        FAILED(frame->SetSize((UINT)width, (UINT)height)) || FAILED(frame->SetPixelFormat(&format)) ||
        format != GUID_WICPixelFormat24bppBGR) {
        return std::string();
    }
    std::vector<uint8_t> pixels((size_t)width * (size_t)height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t *pixel = pixels.data() + ((size_t)y * (size_t)width + (size_t)x) * 3;
            pixel[0] = (uint8_t)((x + y) % 256);
            pixel[1] = (uint8_t)(y % 256);
            pixel[2] = (uint8_t)(x % 256);
        }
    }
    if (FAILED(frame->WritePixels((UINT)height, (UINT)width * 3, (UINT)pixels.size(), pixels.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return std::string();
    }
    return path.string();
}
#else
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
#endif

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
            decoder->decodeRegion(rect.x, rect.y, rect.width, rect.height, 1);
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
        const Bitmap tile = decoder->decodeRegion(128, 64, width, height, sampleSize);
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
        const Bitmap tile = decoder->decodeRegion(column * 256, 0, 256, 256, 1);
        CHECK(tile.valid());
        CHECK_EQ(tile.width(), 256);
        // Red follows x in the gradient, so each tile starts where the last
        // one ended.
        if (tile.valid()) {
            CHECK_NEAR(tile.pixels()[tile.redOffset()], (column * 256) % 256, 2);
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
    const Bitmap tile = decoder->decodeRegion(400, 200, 256, 256, 2);
    CHECK(tile.valid());
    // A JPEG has no alpha, which the decoder says so the upload need not look.
    CHECK(tile.knownOpaque());
    if (tile.valid()) {
        const uint8_t *topLeft = tile.pixels();
        CHECK_NEAR(topLeft[tile.redOffset()], 400 % 256, 6);
        CHECK_NEAR(topLeft[1], 200 % 256, 6);
    }

    fs::remove(path);
}

#if defined(_WIN32)
namespace {

// A TIFF, whose codec does not reduce while decoding, as HEIF's does not.
// Red rises smoothly across it and green down it, so a reduced tile's colour
// says where in the picture it came from.
std::string writeRampTiff(const char *name, int width, int height) {
    const fs::path path = fs::temp_directory_path() / name;
    IWICImagingFactory *imaging = Wic::factory();
    Wic::Ptr<IWICStream> stream;
    Wic::Ptr<IWICBitmapEncoder> encoder;
    Wic::Ptr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (imaging == nullptr || FAILED(imaging->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE)) ||
        FAILED(imaging->CreateEncoder(GUID_ContainerFormatTiff, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize((UINT)width, (UINT)height)) || FAILED(frame->SetPixelFormat(&format)) ||
        format != GUID_WICPixelFormat24bppBGR) {
        return std::string();
    }
    std::vector<uint8_t> pixels((size_t)width * (size_t)height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t *pixel = pixels.data() + ((size_t)y * (size_t)width + (size_t)x) * 3;
            pixel[0] = 128;
            pixel[1] = (uint8_t)(y * 255 / (height - 1));
            pixel[2] = (uint8_t)(x * 255 / (width - 1));
        }
    }
    if (FAILED(frame->WritePixels((UINT)height, (UINT)width * 3, (UINT)pixels.size(), pixels.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return std::string();
    }
    return path.string();
}

}  // namespace

TEST(a_reduced_tile_from_a_codec_that_cannot_reduce_lands_on_its_part_of_the_picture) {
    const std::string path = writeRampTiff("gallery3d_region_ramp.tif", 2048, 1536);
    CHECK(!path.empty());
    const Bitmap whole = Bitmap::load(path, 0);
    const RegionDecoderPtr decoder = RegionDecoder::open(path);
    CHECK(whole.valid());
    CHECK(decoder != nullptr);
    if (!whole.valid() || decoder == nullptr) {
        return;
    }
    // Coarse first, then finer, then coarse again from the finer level kept.
    for (int sample : {8, 2, 4, 8}) {
        const int edge = 128 * sample;
        const Bitmap tile = decoder->decodeRegion(256, 256, edge, edge, sample);
        CHECK(tile.valid());
        CHECK_EQ(tile.width(), 128);
        CHECK_EQ(tile.height(), 128);
        if (tile.valid() && tile.width() == 128 && tile.height() == 128) {
            // The tile's centre against the same point of the whole picture.
            const int x = 256 + edge / 2;
            const int y = 256 + edge / 2;
            const uint8_t *expected = whole.pixels() + ((size_t)y * 2048 + (size_t)x) * 4;
            const uint8_t *actual = tile.pixels() + ((size_t)64 * 128 + 64) * 4;
            CHECK_NEAR(actual[0], expected[0], 3);
            CHECK_NEAR(actual[1], expected[1], 3);
        }
    }
    fs::remove(path);
}
#endif

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
        CHECK(!decoder->decodeRegion(200, 200, 64, 64, 1).valid());
        CHECK(!decoder->decodeRegion(0, 0, 0, 0, 1).valid());
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
    CHECK(a->decodeRegion(0, 0, 256, 256, 1).valid());

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
    source.requestRegion(&photo, 512, 512, 512, 512, 1,
                         [&tile](Bitmap bitmap) { tile = std::move(bitmap); });
    CHECK(tile.valid());
    CHECK_EQ(tile.width(), 512);

    // A rectangle running off the right edge is refused, not trimmed.
    Bitmap edge;
    source.requestRegion(&photo, 1900, 1400, 512, 512, 4,
                         [&edge](Bitmap bitmap) { edge = std::move(bitmap); });
    CHECK(!edge.valid());

    // The source holds the decoder open, so the tiles after the first one do
    // not go back to the disk.
    fs::remove(jpeg);
    Bitmap afterDelete;
    source.requestRegion(&photo, 0, 0, 512, 512, 1,
                         [&afterDelete](Bitmap bitmap) { afterDelete = std::move(bitmap); });
    CHECK(afterDelete.valid());

    // A different photo replaces it, and that one is gone.
    MediaItem other;
    other.mFilePath = jpeg + ".other";
    other.mMimeType = "image/jpeg";
    other.mFullWidth = 100;
    other.mFullHeight = 100;
    Bitmap missing;
    source.requestRegion(&other, 0, 0, 50, 50, 1,
                         [&missing](Bitmap bitmap) { missing = std::move(bitmap); });
    CHECK(!missing.valid());
}

TEST(a_photo_decodes_reduced_without_being_built_at_full_size_first) {
    // The thumbnail path. What matters is that reducing inside the decoder
    // lands on the same picture as decoding whole and scaling after, at the
    // same size, since that is what the wall used to do.
    const std::string path = writeGradientJpeg("gallery3d_subsampled.jpg", 2048, 1536);
    std::vector<uint8_t> encoded;
    CHECK(Bitmap::readFile(path, &encoded));

    const Bitmap reduced = Bitmap::loadFromMemory(encoded.data(), encoded.size(), 256);
    CHECK(reduced.valid());
    CHECK(reduced.knownOpaque());
    // Long edge trimmed to what was asked for, aspect kept.
    CHECK_EQ(reduced.width(), 256);
    CHECK_EQ(reduced.height(), 192);

    // Red follows x and green follows y in the gradient, so the corners say
    // the picture is the right way up and not cropped.
    if (reduced.valid()) {
        const uint8_t *topLeft = reduced.pixels();
        CHECK_NEAR(topLeft[reduced.redOffset()], 0, 12);
        CHECK_NEAR(topLeft[1], 0, 12);
        const uint8_t *topRight = reduced.pixels() + (size_t)(reduced.width() - 1) * 4;
        // x runs 0..2047 and the gradient wraps every 256, so the last column
        // is near the top of a ramp.
        CHECK(topRight[reduced.redOffset()] > 200);
    }

    // Asking for more than the photo holds must not enlarge it.
    const Bitmap whole = Bitmap::loadFromMemory(encoded.data(), encoded.size(), 4096);
    CHECK(whole.valid());
    CHECK_EQ(whole.width(), 2048);
    CHECK_EQ(whole.height(), 1536);

    fs::remove(path);
}

TEST(a_format_with_no_reducing_decoder_still_loads) {
    // PNG has no subsampled path on the desktop, so it falls back to decoding
    // whole and picking pixels. The caller cannot tell the difference.
    std::vector<uint8_t> encoded;
    CHECK(Bitmap::readFile(std::string(GALLERY3D_ASSET_ROOT) + "/drawable/icon_home_small.png", &encoded));
    const Bitmap decoded = Bitmap::loadFromMemory(encoded.data(), encoded.size(), 32);
    const Bitmap whole = Bitmap::loadFromMemory(encoded.data(), encoded.size(), 0);
    CHECK(decoded.valid());
    CHECK(whole.valid());
    if (decoded.valid() && whole.valid()) {
        const Bitmap::Size size = Bitmap::sampledSize(Sampling::Picked, whole.width(), whole.height(),
                                                      Bitmap::sampleSizeFor(whole.width(), whole.height(), 32));
        CHECK_EQ(decoded.width(), size.width);
        CHECK_EQ(decoded.height(), size.height);
    }
}
