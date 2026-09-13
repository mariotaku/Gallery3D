// Bitmap arithmetic: padding, cropping, and the EXIF reader.
#include "tests.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "App.h"
#include "Bitmap.h"
#include "Shared.h"

namespace {

Bitmap solid(int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    Bitmap bitmap(width, height);
    uint8_t *pixels = bitmap.pixels();
    for (int i = 0; i < width * height; ++i) {
        pixels[i * 4 + 0] = red;
        pixels[i * 4 + 1] = green;
        pixels[i * 4 + 2] = blue;
        pixels[i * 4 + 3] = alpha;
    }
    return bitmap;
}

const uint8_t *pixelAt(const Bitmap &bitmap, int x, int y) {
    return bitmap.pixels() + ((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4;
}

// A JPEG carrying nothing but an APP1 segment, so the EXIF reader can be tested
// without shipping a photo. Not decodable as an image, which is fine: readExif
// only walks the markers.
std::string writeExifJpeg(const std::string &name, const std::vector<uint8_t> &ifdEntries,
                          const std::vector<uint8_t> &trailing) {
    std::vector<uint8_t> tiff;
    auto put16 = [&tiff](unsigned value) {
        tiff.push_back((uint8_t)(value & 0xFF));
        tiff.push_back((uint8_t)((value >> 8) & 0xFF));
    };
    auto put32 = [&tiff](unsigned value) {
        tiff.push_back((uint8_t)(value & 0xFF));
        tiff.push_back((uint8_t)((value >> 8) & 0xFF));
        tiff.push_back((uint8_t)((value >> 16) & 0xFF));
        tiff.push_back((uint8_t)((value >> 24) & 0xFF));
    };
    tiff.push_back('I');
    tiff.push_back('I');
    put16(42);
    put32(8);
    put16((unsigned)(ifdEntries.size() / 12));
    tiff.insert(tiff.end(), ifdEntries.begin(), ifdEntries.end());
    put32(0);
    tiff.insert(tiff.end(), trailing.begin(), trailing.end());

    std::vector<uint8_t> file{0xFF, 0xD8, 0xFF, 0xE1};
    size_t length = tiff.size() + 8;
    file.push_back((uint8_t)((length >> 8) & 0xFF));
    file.push_back((uint8_t)(length & 0xFF));
    const char *marker = "Exif\0\0";
    file.insert(file.end(), marker, marker + 6);
    file.insert(file.end(), tiff.begin(), tiff.end());
    file.push_back(0xFF);
    file.push_back(0xD9);

    // The temp folder, not the checkout, which a build may only be able to read.
    std::string path = (std::filesystem::temp_directory_path() / name).string();
    std::FILE *out = std::fopen(path.c_str(), "wb");
    if (out != nullptr) {
        std::fwrite(file.data(), 1, file.size(), out);
        std::fclose(out);
    }
    return path;
}

// One 12 byte IFD entry, little endian, with the value inline.
void appendEntry(std::vector<uint8_t> &entries, unsigned tag, unsigned type, unsigned count,
                 unsigned value) {
    auto put16 = [&entries](unsigned v) {
        entries.push_back((uint8_t)(v & 0xFF));
        entries.push_back((uint8_t)((v >> 8) & 0xFF));
    };
    auto put32 = [&entries](unsigned v) {
        entries.push_back((uint8_t)(v & 0xFF));
        entries.push_back((uint8_t)((v >> 8) & 0xFF));
        entries.push_back((uint8_t)((v >> 16) & 0xFF));
        entries.push_back((uint8_t)((v >> 24) & 0xFF));
    };
    put16(tag);
    put16(type);
    put32(count);
    put32(value);
}

}  // namespace

TEST(padded_to_leaves_the_extra_transparent_by_default) {
    Bitmap source = solid(2, 2, 10, 20, 30, 255);
    Bitmap padded = source.paddedTo(4, 4);
    CHECK_EQ(padded.width(), 4);
    CHECK_EQ(padded.height(), 4);
    // The image lands in the top left.
    CHECK_EQ((int)pixelAt(padded, 0, 0)[0], 10);
    // Everything past it stays empty, which is what a quad with real extents
    // never samples.
    CHECK_EQ((int)pixelAt(padded, 3, 3)[3], 0);
    CHECK_EQ((int)pixelAt(padded, 2, 0)[3], 0);
}

TEST(padded_to_can_repeat_the_edge_for_mipmapping) {
    Bitmap source = solid(2, 2, 10, 20, 30, 255);
    Bitmap padded = source.paddedTo(4, 4, true);
    // A reduced mip level averages across the boundary, so the padding has to
    // carry the edge colour rather than transparency.
    CHECK_EQ((int)pixelAt(padded, 3, 3)[3], 255);
    CHECK_EQ((int)pixelAt(padded, 3, 3)[0], 10);
    CHECK_EQ((int)pixelAt(padded, 2, 0)[0], 10);
    CHECK_EQ((int)pixelAt(padded, 0, 2)[0], 10);
}

TEST(cover_cropped_fills_the_box_without_letterboxing) {
    // Wider than the target, so the sides are what gets trimmed.
    Bitmap source = solid(400, 100, 1, 2, 3, 255);
    Bitmap cropped = source.coverCropped(100, 100);
    CHECK_EQ(cropped.width(), 100);
    CHECK_EQ(cropped.height(), 100);
    // Every pixel is image, none is padding.
    CHECK_EQ((int)pixelAt(cropped, 0, 0)[3], 255);
    CHECK_EQ((int)pixelAt(cropped, 99, 99)[3], 255);
}

TEST(next_power_of_two_is_what_the_padding_relies_on) {
    CHECK_EQ(Shared::nextPowerOf2(1), 1);
    CHECK_EQ(Shared::nextPowerOf2(2), 2);
    CHECK_EQ(Shared::nextPowerOf2(3), 4);
    CHECK_EQ(Shared::nextPowerOf2(252), 256);
    // 128 * 2.625 rounds to 336, and this is why a thumbnail texture is 512.
    CHECK_EQ(Shared::nextPowerOf2(336), 512);
    CHECK_EQ(Shared::nextPowerOf2(1280), 2048);
}

TEST(exif_reads_the_orientation) {
    struct Case {
        unsigned tagValue;
        float degrees;
    };
    const Case cases[] = {{1, 0.0f}, {3, 180.0f}, {6, 90.0f}, {8, 270.0f}, {99, 0.0f}};
    for (const Case &testCase : cases) {
        std::vector<uint8_t> entries;
        appendEntry(entries, 0x0112, 3, 1, testCase.tagValue);
        std::string path = writeExifJpeg("exif_orientation_test.jpg", entries, {});
        Bitmap::ExifInfo info = Bitmap::readExif(path);
        CHECK_NEAR(info.rotationDegrees, testCase.degrees, 0.001);
        std::remove(path.c_str());
    }
}

TEST(exif_reads_a_gps_position) {
    // 35 41' 4.32" N, 139 50' 23.4" E, held as three rationals each.
    std::vector<uint8_t> trailing;
    auto putRational = [&trailing](unsigned numerator, unsigned denominator) {
        for (unsigned value : {numerator, denominator}) {
            trailing.push_back((uint8_t)(value & 0xFF));
            trailing.push_back((uint8_t)((value >> 8) & 0xFF));
            trailing.push_back((uint8_t)((value >> 16) & 0xFF));
            trailing.push_back((uint8_t)((value >> 24) & 0xFF));
        }
    };

    std::vector<uint8_t> gpsEntries;
    // The GPS IFD sits after IFD0, whose four entries and terminator are known
    // in size, so the offsets can be worked out up front.
    const unsigned ifd0Size = 2 + 12 * 1 + 4;
    const unsigned gpsIfdOffset = 8 + ifd0Size;
    const unsigned gpsEntryCount = 4;
    const unsigned valuesOffset = gpsIfdOffset + 2 + 12 * gpsEntryCount + 4;

    appendEntry(gpsEntries, 0x0001, 2, 2, (unsigned)'N');
    appendEntry(gpsEntries, 0x0002, 5, 3, valuesOffset);
    appendEntry(gpsEntries, 0x0003, 2, 2, (unsigned)'E');
    appendEntry(gpsEntries, 0x0004, 5, 3, valuesOffset + 24);

    std::vector<uint8_t> ifd0;
    appendEntry(ifd0, 0x8825, 4, 1, gpsIfdOffset);

    // Everything after IFD0: the GPS IFD, then the coordinate values.
    std::vector<uint8_t> after;
    after.push_back((uint8_t)(gpsEntryCount & 0xFF));
    after.push_back((uint8_t)((gpsEntryCount >> 8) & 0xFF));
    after.insert(after.end(), gpsEntries.begin(), gpsEntries.end());
    for (int i = 0; i < 4; ++i) {
        after.push_back(0);
    }
    trailing = after;
    putRational(35, 1);
    putRational(41, 1);
    putRational(432, 100);
    putRational(139, 1);
    putRational(50, 1);
    putRational(234, 10);

    std::string path = writeExifJpeg("exif_gps_test.jpg", ifd0, trailing);
    Bitmap::ExifInfo info = Bitmap::readExif(path);
    CHECK_NEAR(info.latitude, 35.6845, 0.001);
    CHECK_NEAR(info.longitude, 139.8398, 0.001);
    std::remove(path.c_str());
}

TEST(exif_on_a_file_without_it_reports_nothing) {
    std::string path = std::string(GALLERY3D_ASSET_ROOT) + "/drawable/icon_home_small.png";
    Bitmap::ExifInfo info = Bitmap::readExif(path);
    CHECK_NEAR(info.rotationDegrees, 0.0, 0.001);
    CHECK_EQ(info.dateTakenMs, (int64_t)0);
    // Zero is how the port spells "no position", and a PNG has none.
    CHECK_NEAR(info.latitude, 0.0, 1e-12);
    CHECK_NEAR(info.longitude, 0.0, 1e-12);
}
