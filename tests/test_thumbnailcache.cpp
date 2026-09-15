// The freedesktop.org thumbnail cache: the name a thumbnail is filed under, and
// when a cached thumbnail stands in for a decode.
#include "tests.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/Md5.h"
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

uint32_t crc32Of(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void pushBigEndian(std::vector<uint8_t> &bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back((uint8_t)(value >> shift));
    }
}

// The PNG with a tEXt chunk of key and value after its header chunk.
std::vector<uint8_t> withText(std::vector<uint8_t> png, const std::string &key, const std::string &value) {
    std::vector<uint8_t> chunk;
    const std::string body = key + std::string(1, '\0') + value;
    pushBigEndian(chunk, (uint32_t)body.size());
    const size_t typeAt = chunk.size();
    chunk.insert(chunk.end(), {'t', 'E', 'X', 't'});
    chunk.insert(chunk.end(), body.begin(), body.end());
    pushBigEndian(chunk, crc32Of(&chunk[typeAt], chunk.size() - typeAt));
    // After the signature and the header chunk, 8 and 25 bytes.
    png.insert(png.begin() + 33, chunk.begin(), chunk.end());
    return png;
}

// A picture red in one quarter, top left or top right, and blue elsewhere.
Bitmap quartered(int width, int height, bool redAtRight) {
    Bitmap bitmap(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool red = y < height / 2 && (redAtRight ? x >= width / 2 : x < width / 2);
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
        SDL_PathInfo info;
        if (SDL_GetPathInfo(photo.c_str(), &info)) {
            modified = (long long)(info.modify_time / SDL_NS_PER_SECOND);
        }
    }
    ~Cache() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    // Files thumbnail in the size folder, as a thumbnailer would, dated mtime.
    bool put(const std::string &folder, const Bitmap &thumbnail, long long mtime) const {
        const std::string encoded = (fs::path(root) / "encoded.png").string();
        std::vector<uint8_t> png;
        if (!thumbnail.savePng(encoded) || !Bitmap::readFile(encoded, &png)) {
            return false;
        }
        png = withText(png, "Thumb::URI", ThumbnailCache::uriFor(photo));
        png = withText(png, "Thumb::MTime", std::to_string(mtime));
        const fs::path directory = fs::path(root) / folder;
        std::error_code error;
        fs::create_directories(directory, error);
        std::ofstream out(directory / (Md5::hex(ThumbnailCache::uriFor(photo)) + ".png"), std::ios::binary);
        out.write((const char *)png.data(), (std::streamsize)png.size());
        return (bool)out;
    }

    std::string root;
    std::string photo;
    long long modified = 0;
};

}  // namespace

TEST(a_cached_thumbnail_is_scaled_to_the_edge_asked_for) {
    Cache cache;
    CHECK(cache.put("x-large", quartered(480, 320, false), cache.modified));
    // Nothing in large, so the lookup goes on to x-large.
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
    CHECK(cache.put("x-large", quartered(480, 320, false), cache.modified));
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.photo, 512).valid());
}

TEST(a_thumbnail_of_a_photo_changed_since_is_not_used) {
    Cache cache;
    CHECK(cache.put("x-large", quartered(480, 320, false), cache.modified - 1));
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.photo, 240).valid());
}

TEST(a_photo_with_no_thumbnail_has_none) {
    Cache cache;
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.photo, 240).valid());
    CHECK(!ThumbnailCache::loadUpright(cache.root, cache.root + "/missing.jpg", 240).valid());
}

TEST(an_upright_thumbnail_turns_back_to_the_stored_pixels) {
    // Stored landscape with red top left, and tagged to show a quarter turn
    // clockwise: upright it is portrait with red top right.
    const Bitmap stored = ThumbnailCache::turnedBack(quartered(320, 480, true), 90.0f);
    CHECK_EQ(stored.width(), 480);
    CHECK_EQ(stored.height(), 320);
    CHECK(isRed(stored, 120, 80));
    CHECK(isBlue(stored, 360, 80));
    CHECK(isBlue(stored, 120, 240));

    // Half a turn: red top left comes back bottom right, and so on round.
    const Bitmap half = ThumbnailCache::turnedBack(quartered(480, 320, false), 180.0f);
    CHECK_EQ(half.width(), 480);
    CHECK(isRed(half, 360, 240));
    CHECK(isBlue(half, 120, 80));

    // Three quarters: shown turned clockwise by 270, upright red is bottom
    // left of a portrait, which is the stored top left.
    Bitmap bottomLeft(320, 480);
    const Bitmap source = quartered(320, 480, false);
    for (int y = 0; y < 480; ++y) {
        std::memcpy(bottomLeft.pixels() + (size_t)y * 320 * 4, source.pixels() + (size_t)(479 - y) * 320 * 4, 320 * 4);
    }
    const Bitmap threeQuarters = ThumbnailCache::turnedBack(bottomLeft, 270.0f);
    CHECK_EQ(threeQuarters.width(), 480);
    CHECK(isRed(threeQuarters, 120, 80));
    CHECK(isBlue(threeQuarters, 360, 240));
}
