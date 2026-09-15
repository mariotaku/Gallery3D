// FileOperations::setExifOrientation on every platform: the tag a turn writes,
// and the files it leaves alone.
#include "tests.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "decode_fixtures.h"
#include "graphics/Bitmap.h"
#include "media/FileOperations.h"

namespace fs = std::filesystem;

namespace {

// A copy of the fixture in the temp folder, removed when the test is done.
struct Copy {
    Copy(const std::string &file, const std::string &name) : path((fs::temp_directory_path() / name).string()) {
        std::error_code error;
        fs::copy_file(DecodeFixtures::folder() + file, path, fs::copy_options::overwrite_existing, error);
    }
    ~Copy() {
        std::error_code error;
        fs::remove(path, error);
    }
    std::string path;
};

}  // namespace

TEST(a_turn_writes_the_orientation_that_shows_it) {
    const Copy copy("orientation_1.jpg", "gallery3d_orientation_write.jpg");
    struct Turn {
        float degrees;
        int orientation;
    };
    // Any whole number of turns either way, rounded to the nearest degree.
    // Anything but a quarter turn is upright.
    const Turn turns[] = {
        {90.0f, 6},   {180.0f, 3}, {270.0f, 8},  {0.0f, 1},   {360.0f, 1},
        {-90.0f, 8},  {-180.0f, 3}, {-270.0f, 6}, {450.0f, 6}, {89.6f, 6},
        {45.0f, 1},
    };
    for (const Turn &turn : turns) {
        const std::string what = std::to_string(turn.degrees) + " degrees";
        CHECK_DETAIL(FileOperations::setExifOrientation(copy.path, turn.degrees), what + ": not written");
        const int written = Bitmap::readExif(copy.path).orientation;
        CHECK_DETAIL(written == turn.orientation, what + ": wrote " + std::to_string(written));
    }
    // The tag changes and the pixels do not.
    const Bitmap after = Bitmap::load(copy.path, 0);
    CHECK(after.valid());
    CHECK_EQ(after.width(), 320);
}

TEST(a_file_with_no_orientation_tag_is_left_alone) {
    for (const char *file : {"baseline.jpg", "opaque.png"}) {
        const Copy copy(file, std::string("gallery3d_orientation_none_") + file);
        std::vector<uint8_t> before;
        std::vector<uint8_t> after;
        CHECK(Bitmap::readFile(copy.path, &before));
        CHECK_DETAIL(!FileOperations::setExifOrientation(copy.path, 90.0f), std::string(file) + ": written");
        CHECK(Bitmap::readFile(copy.path, &after));
        CHECK_DETAIL(before == after, std::string(file) + ": changed");
    }
    CHECK(!FileOperations::setExifOrientation((fs::temp_directory_path() / "gallery3d_missing.jpg").string(), 90.0f));
}
