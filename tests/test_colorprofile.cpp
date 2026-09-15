// A photo with an embedded colour profile decodes to sRGB, whole, scaled and
// tile by tile.
//
// The profile here is sRGB with its red and green primaries exchanged, so a
// pixel stored as pure red is pure green once converted. A decode that ignores
// the profile leaves it red.
#include "tests.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "platform/windows/Wic.h"
#else
#include <jpeglib.h>
#endif

#include "graphics/Bitmap.h"
#include "graphics/RegionDecoder.h"

namespace fs = std::filesystem;

namespace {

void putBig32(std::vector<uint8_t> &bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back((uint8_t)(value >> shift));
    }
}

void putBig16(std::vector<uint8_t> &bytes, uint16_t value) {
    bytes.push_back((uint8_t)(value >> 8));
    bytes.push_back((uint8_t)value);
}

void putSignature(std::vector<uint8_t> &bytes, const char *signature) {
    bytes.insert(bytes.end(), signature, signature + 4);
}

void padTo4(std::vector<uint8_t> &bytes) {
    while (bytes.size() % 4 != 0) {
        bytes.push_back(0);
    }
}

uint32_t fixed16(double value) {
    return (uint32_t)(int32_t)(value * 65536.0 + (value < 0.0 ? -0.5 : 0.5));
}

std::vector<uint8_t> xyzTag(double x, double y, double z) {
    std::vector<uint8_t> tag;
    putSignature(tag, "XYZ ");
    putBig32(tag, 0);
    putBig32(tag, fixed16(x));
    putBig32(tag, fixed16(y));
    putBig32(tag, fixed16(z));
    return tag;
}

// A 2.2 gamma.
std::vector<uint8_t> curveTag() {
    std::vector<uint8_t> tag;
    putSignature(tag, "curv");
    putBig32(tag, 0);
    putBig32(tag, 1);
    putBig16(tag, 0x0233);
    padTo4(tag);
    return tag;
}

std::vector<uint8_t> textTag(const char *type, const char *text) {
    std::vector<uint8_t> tag;
    putSignature(tag, type);
    putBig32(tag, 0);
    const size_t length = std::strlen(text) + 1;
    if (std::strcmp(type, "desc") == 0) {
        putBig32(tag, (uint32_t)length);
        tag.insert(tag.end(), text, text + length);
        // No Unicode or ScriptCode description.
        tag.insert(tag.end(), 4 + 4 + 2 + 1 + 67, 0);
    } else {
        tag.insert(tag.end(), text, text + length);
    }
    padTo4(tag);
    return tag;
}

// An ICC v2 display profile: sRGB's primaries adapted to D50, with red and
// green exchanged.
std::vector<uint8_t> swappedProfile() {
    struct Tag {
        const char *signature;
        std::vector<uint8_t> data;
    };
    const std::vector<Tag> tags = {
        {"desc", textTag("desc", "Red and green exchanged")},
        {"cprt", textTag("text", "No copyright")},
        {"wtpt", xyzTag(0.9642, 1.0, 0.8249)},
        {"rXYZ", xyzTag(0.3851, 0.7169, 0.0971)},
        {"gXYZ", xyzTag(0.4361, 0.2225, 0.0139)},
        {"bXYZ", xyzTag(0.1431, 0.0606, 0.7141)},
        {"rTRC", curveTag()},
        {"gTRC", curveTag()},
        {"bTRC", curveTag()},
    };
    std::vector<uint8_t> table;
    std::vector<uint8_t> data;
    const uint32_t dataStart = 128 + 4 + 12 * (uint32_t)tags.size();
    putBig32(table, (uint32_t)tags.size());
    for (const Tag &tag : tags) {
        putSignature(table, tag.signature);
        putBig32(table, dataStart + (uint32_t)data.size());
        putBig32(table, (uint32_t)tag.data.size());
        data.insert(data.end(), tag.data.begin(), tag.data.end());
    }

    std::vector<uint8_t> profile;
    putBig32(profile, 128 + (uint32_t)table.size() + (uint32_t)data.size());
    putBig32(profile, 0);
    putBig32(profile, 0x02100000);
    putSignature(profile, "mntr");
    putSignature(profile, "RGB ");
    putSignature(profile, "XYZ ");
    for (uint16_t part : {2026, 9, 14, 0, 0, 0}) {
        putBig16(profile, part);
    }
    putSignature(profile, "acsp");
    putSignature(profile, "MSFT");
    // Flags, manufacturer, model, attributes and rendering intent.
    profile.insert(profile.end(), 4 + 4 + 4 + 8 + 4, 0);
    putBig32(profile, fixed16(0.9642));
    putBig32(profile, fixed16(1.0));
    putBig32(profile, fixed16(0.8249));
    // Creator, profile ID and the reserved bytes.
    profile.insert(profile.end(), 4 + 16 + 28, 0);
    profile.insert(profile.end(), table.begin(), table.end());
    profile.insert(profile.end(), data.begin(), data.end());
    return profile;
}

#if defined(_WIN32)
// A picture of pure red at alpha, written by WIC's encoder for the container,
// with the swapped profile embedded when withProfile is set.
fs::path writeRed(const std::string &name, REFGUID container, UINT size, BYTE alpha, bool withProfile) {
    const fs::path path = fs::temp_directory_path() / name;
    const bool opaque = alpha == 255;
    const UINT channels = opaque ? 3 : 4;
    std::vector<BYTE> pixels((size_t)size * size * channels);
    for (size_t i = 0; i < pixels.size(); i += channels) {
        // BGR, or straight BGRA.
        pixels[i] = 0;
        pixels[i + 1] = 0;
        pixels[i + 2] = 255;
        if (!opaque) {
            pixels[i + 3] = alpha;
        }
    }
    const std::vector<uint8_t> profile = swappedProfile();
    IWICImagingFactory *imaging = Wic::factory();
    Wic::Ptr<IWICStream> stream;
    Wic::Ptr<IWICBitmapEncoder> encoder;
    Wic::Ptr<IWICBitmapFrameEncode> frame;
    Wic::Ptr<IWICColorContext> context;
    WICPixelFormatGUID format = opaque ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
    bool written = imaging != nullptr && SUCCEEDED(imaging->CreateStream(stream.put())) &&
                   SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
                   SUCCEEDED(imaging->CreateEncoder(container, nullptr, encoder.put())) &&
                   SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) &&
                   SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
                   SUCCEEDED(frame->SetSize(size, size)) && SUCCEEDED(frame->SetPixelFormat(&format));
    if (written && withProfile) {
        IWICColorContext *slot = nullptr;
        written = SUCCEEDED(imaging->CreateColorContext(context.put())) &&
                  SUCCEEDED(context->InitializeFromMemory(profile.data(), (UINT)profile.size()));
        slot = context.get();
        written = written && SUCCEEDED(frame->SetColorContexts(1, &slot));
    }
    written = written && SUCCEEDED(frame->WritePixels(size, size * channels, (UINT)pixels.size(), pixels.data())) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    return written ? path : fs::path();
}

fs::path writeRedJpeg(const std::string &name, int size, bool withProfile) {
    return writeRed(name, GUID_ContainerFormatJpeg, (UINT)size, 255, withProfile);
}
#else
// A JPEG of pure red, written by libjpeg, with the swapped profile embedded
// when withProfile is set.
fs::path writeRedJpeg(const std::string &name, int size, bool withProfile) {
    const fs::path path = fs::temp_directory_path() / name;
    std::FILE *file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return fs::path();
    }
    jpeg_compress_struct cinfo {};
    jpeg_error_mgr error {};
    cinfo.err = jpeg_std_error(&error);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, file);
    cinfo.image_width = (JDIMENSION)size;
    cinfo.image_height = (JDIMENSION)size;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 95, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    if (withProfile) {
        const std::vector<uint8_t> profile = swappedProfile();
        jpeg_write_icc_profile(&cinfo, profile.data(), (unsigned int)profile.size());
    }
    std::vector<uint8_t> row((size_t)size * 3, 0);
    for (size_t i = 0; i < row.size(); i += 3) {
        row[i] = 255;
    }
    for (int y = 0; y < size; ++y) {
        JSAMPROW rows[1] = {row.data()};
        jpeg_write_scanlines(&cinfo, rows, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    std::fclose(file);
    return path;
}
#endif

const uint8_t *centreOf(const Bitmap &bitmap) {
    return bitmap.pixels() + ((size_t)(bitmap.height() / 2) * (size_t)bitmap.width() + (size_t)bitmap.width() / 2) * 4;
}

bool isGreen(const Bitmap &bitmap) {
    if (!bitmap.valid()) {
        return false;
    }
    const uint8_t *pixel = centreOf(bitmap);
    return pixel[bitmap.redOffset()] < 40 && pixel[1] > 215 && pixel[bitmap.blueOffset()] < 40 && pixel[3] == 255;
}

bool isRed(const Bitmap &bitmap) {
    if (!bitmap.valid()) {
        return false;
    }
    const uint8_t *pixel = centreOf(bitmap);
    return pixel[bitmap.redOffset()] > 215 && pixel[1] < 40 && pixel[bitmap.blueOffset()] < 40 && pixel[3] == 255;
}

}  // namespace

TEST(a_photo_with_a_colour_profile_decodes_to_srgb) {
    const fs::path path = writeRedJpeg("gallery3d_profile.jpg", 64, true);
    CHECK(!path.empty());
    // At its own size, and reduced, which on Windows puts the conversion behind
    // the scaler.
    CHECK(isGreen(Bitmap::load(path.string(), 0)));
    CHECK(isGreen(Bitmap::load(path.string(), 32)));
    // A conversion can hand out alpha, but the JPEG under it has none.
    CHECK(Bitmap::load(path.string(), 0).knownOpaque());
    CHECK(Bitmap::load(path.string(), 32).knownOpaque());
    std::error_code error;
    fs::remove(path, error);
}

TEST(a_photo_without_a_colour_profile_keeps_its_colours) {
    const fs::path path = writeRedJpeg("gallery3d_no_profile.jpg", 64, false);
    CHECK(!path.empty());
    CHECK(isRed(Bitmap::load(path.string(), 0)));
    CHECK(isRed(Bitmap::load(path.string(), 32)));
    std::error_code error;
    fs::remove(path, error);
}

#if defined(_WIN32)
TEST(a_translucent_picture_with_a_colour_profile_is_converted_before_premultiplying) {
    const fs::path path = writeRed("gallery3d_profile.png", GUID_ContainerFormatPng, 64, 128, true);
    CHECK(!path.empty());
    for (int maxEdge : {0, 32}) {
        const Bitmap bitmap = Bitmap::load(path.string(), maxEdge);
        CHECK(bitmap.valid());
        CHECK(!bitmap.knownOpaque());
        if (bitmap.valid()) {
            // Green at half coverage, premultiplied.
            const uint8_t *pixel = centreOf(bitmap);
            CHECK(pixel[0] < 20);
            CHECK_NEAR(pixel[1], 128, 6);
            CHECK(pixel[2] < 20);
            CHECK_NEAR(pixel[3], 128, 2);
        }
    }
    std::error_code error;
    fs::remove(path, error);
}
#endif

TEST(a_tile_of_a_photo_with_a_colour_profile_is_in_srgb) {
    const fs::path path = writeRedJpeg("gallery3d_profile_tiles.jpg", 64, true);
    CHECK(!path.empty());
    RegionDecoderPtr decoder = RegionDecoder::open(path.string());
    CHECK(decoder != nullptr);
    if (decoder != nullptr) {
        CHECK(isGreen(decoder->decodeRegion(0, 0, 64, 64, 1)));
        CHECK(isGreen(decoder->decodeRegion(16, 16, 32, 32, 2)));
    }
    decoder.reset();
    std::error_code error;
    fs::remove(path, error);
}
