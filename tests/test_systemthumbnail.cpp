// The platform's thumbnail of a photo on disk, handed back the way a decode of
// the file gives it: in the orientation its pixels are stored in, and never
// larger than the picture.
#include "tests.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "graphics/SystemThumbnail.h"

#if defined(_WIN32)
#include "platform/windows/Wic.h"

namespace fs = std::filesystem;

namespace {

// A JPEG of width by height with an EXIF orientation tag, red in the top left
// quarter of its stored pixels and blue everywhere else.
fs::path writeQuarteredJpeg(const std::string &name, UINT width, UINT height, USHORT orientation) {
    const fs::path path = fs::temp_directory_path() / name;
    std::vector<BYTE> pixels((size_t)width * height * 3);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const bool red = x < width / 2 && y < height / 2;
            BYTE *pixel = &pixels[((size_t)y * width + x) * 3];
            // BGR.
            pixel[0] = red ? 0 : 255;
            pixel[1] = 0;
            pixel[2] = red ? 255 : 0;
        }
    }
    IWICImagingFactory *imaging = Wic::factory();
    Wic::Ptr<IWICStream> stream;
    Wic::Ptr<IWICBitmapEncoder> encoder;
    Wic::Ptr<IWICBitmapFrameEncode> frame;
    Wic::Ptr<IWICMetadataQueryWriter> metadata;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    PROPVARIANT value;
    PropVariantInit(&value);
    value.vt = VT_UI2;
    value.uiVal = orientation;
    const bool written =
        imaging != nullptr && SUCCEEDED(imaging->CreateStream(stream.put())) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(imaging->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put())) &&
        SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        SUCCEEDED(frame->GetMetadataQueryWriter(metadata.put())) &&
        SUCCEEDED(metadata->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value)) &&
        SUCCEEDED(frame->WritePixels(height, width * 3, (UINT)pixels.size(), pixels.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    return written ? path : fs::path();
}

bool isRed(const Bitmap &bitmap, int x, int y) {
    const uint8_t *pixel = bitmap.pixels() + ((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4;
    return pixel[bitmap.redOffset()] > 200 && pixel[bitmap.blueOffset()] < 60 && pixel[3] == 255;
}

bool isBlue(const Bitmap &bitmap, int x, int y) {
    const uint8_t *pixel = bitmap.pixels() + ((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4;
    return pixel[bitmap.redOffset()] < 60 && pixel[bitmap.blueOffset()] > 200 && pixel[3] == 255;
}

}  // namespace

TEST(a_system_thumbnail_comes_back_in_the_stored_orientation) {
    // The shell shows every orientation upright. Whatever it turned or
    // flipped, the red quarter has to be back in the top left of a landscape
    // picture.
    for (USHORT orientation = 1; orientation <= 8; ++orientation) {
        const fs::path path =
            writeQuarteredJpeg("gallery3d_thumbnail_" + std::to_string(orientation) + ".jpg", 1200, 800, orientation);
        CHECK(!path.empty());
        const Bitmap thumbnail = SystemThumbnail::load(path.string(), 512);
        CHECK(thumbnail.valid());
        CHECK(thumbnail.knownOpaque());
        if (thumbnail.valid()) {
            CHECK_EQ(thumbnail.width(), 512);
            CHECK_NEAR(thumbnail.height(), 341, 1);
            CHECK(isRed(thumbnail, 64, 64));
            CHECK(isBlue(thumbnail, 448, 64));
            CHECK(isBlue(thumbnail, 64, 280));
            CHECK(isBlue(thumbnail, 448, 280));
        }
        std::error_code error;
        fs::remove(path, error);
    }
}

TEST(a_system_thumbnail_is_never_enlarged) {
    // A picture smaller than the request has no thumbnail that reaches it.
    const fs::path path = writeQuarteredJpeg("gallery3d_thumbnail_small.jpg", 300, 200, 1);
    CHECK(!path.empty());
    CHECK(!SystemThumbnail::load(path.string(), 512).valid());
    std::error_code error;
    fs::remove(path, error);
}

TEST(a_missing_file_has_no_system_thumbnail) {
    const fs::path path = fs::temp_directory_path() / "gallery3d_thumbnail_missing.jpg";
    CHECK(!SystemThumbnail::load(path.string(), 256).valid());
}

#else

TEST(no_system_thumbnail_off_windows) {
    CHECK(!SystemThumbnail::load("photo.jpg", 256).valid());
}

#endif
