// The freedesktop.org thumbnail cache: the name a thumbnail is filed under, and
// when a cached thumbnail stands in for a decode.
#include "tests.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "core/Md5.h"
#include "freedesktop_cache.h"
#include "graphics/Bitmap.h"
#include "graphics/ThumbnailCache.h"

namespace fs = std::filesystem;

TEST(md5_matches_the_rfc_1321_test_suite) {
    CHECK(Md5::hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(Md5::hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(Md5::hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    // Longer than one block.
    CHECK(Md5::hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
}

TEST(a_thumbnail_is_filed_under_the_md5_of_its_file_uri) {
    // The example in the specification.
    const std::string uri = ThumbnailCache::uriFor("/home/jens/photos/me.png");
    CHECK(uri == "file:///home/jens/photos/me.png");
    CHECK(Md5::hex(uri) == "c6ee772d9e49320e97ec29a7eb5b1697");
}

TEST(a_file_uri_escapes_what_glib_escapes) {
    CHECK(ThumbnailCache::uriFor("/a b/#1;[x].jpg") == "file:///a%20b/%231%3B%5Bx%5D.jpg");
    CHECK(ThumbnailCache::uriFor("/!$&'()*+,-.:=@_~") == "file:///!$&'()*+,-.:=@_~");
    // Every byte of a UTF-8 name.
    CHECK(ThumbnailCache::uriFor("/\xC3\xA9.jpg") == "file:///%C3%A9.jpg");
}

namespace {

// A picture red in its top left quarter and blue elsewhere.
Bitmap quartered(int width, int height) {
    Bitmap bitmap(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool red = y < height / 2 && x < width / 2;
            uint8_t *pixel = bitmap.pixels() + ((size_t)y * (size_t)width + (size_t)x) * 4;
            pixel[bitmap.redOffset()] = red ? 255 : 0;
            pixel[1] = 0;
            pixel[bitmap.blueOffset()] = red ? 0 : 255;
            pixel[3] = 255;
        }
    }
    return bitmap;
}

bool isRed(const Bitmap &bitmap, int x, int y) {
    const uint8_t *pixel = bitmap.pixels() + ((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4;
    return pixel[bitmap.redOffset()] > 200 && pixel[bitmap.blueOffset()] < 60;
}

bool isBlue(const Bitmap &bitmap, int x, int y) {
    const uint8_t *pixel = bitmap.pixels() + ((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4;
    return pixel[bitmap.redOffset()] < 60 && pixel[bitmap.blueOffset()] > 200;
}

// A photo in the temp folder and a thumbnail cache beside it, both removed
// when the test is done.
struct Cache {
    Cache() : root((fs::temp_directory_path() / "gallery3d_thumbnail_cache").string()) {
        std::error_code error;
        fs::remove_all(root, error);
        fs::create_directories(root, error);
        photo = (fs::path(root) / "photo.jpg").string();
        std::ofstream(photo, std::ios::binary) << "not really a photo";
        modified = FreedesktopCache::modifiedSeconds(photo);
    }
    ~Cache() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    bool put(const std::string &folder, const Bitmap &thumbnail, long long mtime) const {
        return FreedesktopCache::put(root, photo, folder, thumbnail, mtime);
    }

    std::string root;
    std::string photo;
    long long modified = 0;
};

}  // namespace

TEST(a_cached_thumbnail_is_reduced_for_the_edge_asked_for) {
    Cache cache;
    CHECK(cache.put("x-large", quartered(480, 320), cache.modified));
    // Nothing in large, so the lookup goes on to x-large, and halving 480
    // still reaches 240.
    const Bitmap thumbnail = ThumbnailCache::loadUpright(cache.root, cache.photo, 240);
    CHECK(thumbnail.valid());
    CHECK(thumbnail.knownOpaque());
    if (thumbnail.valid()) {
        CHECK_EQ(thumbnail.width(), 240);
        CHECK_EQ(thumbnail.height(), 160);
        CHECK(isRed(thumbnail, 60, 40));
        CHECK(isBlue(thumbnail, 180, 40));
        CHECK(isBlue(thumbnail, 60, 120));
    }
}

TEST(a_cached_thumbnail_is_never_enlarged) {
    Cache cache;
    CHECK(cache.put("x-large", quartered(480, 320), cache.modified));
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.photo, 512).valid());
}

TEST(a_thumbnail_of_a_photo_changed_since_is_not_used) {
    Cache cache;
    CHECK(cache.put("x-large", quartered(480, 320), cache.modified - 1));
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.photo, 240).valid());
}

TEST(a_photo_with_no_thumbnail_has_none) {
    Cache cache;
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.photo, 240).valid());
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.root + "/missing.jpg", 240).valid());
}
