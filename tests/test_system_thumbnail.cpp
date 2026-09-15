// SystemThumbnail on every platform against the orientation fixtures. The
// contract is in graphics/SystemThumbnail.h: the platform's thumbnail of a
// photo, handed back the way a decode of the file gives it.
#include "tests.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "decode_fixtures.h"
#include "freedesktop_cache.h"
#include "graphics/Bitmap.h"
#include "graphics/SystemThumbnail.h"
#include "graphics/ThumbnailCache.h"

using namespace DecodeFixtures;
namespace fs = std::filesystem;

namespace {

// How far a thumbnail's patch middles may be from the fixture's. A thumbnailer
// resamples and may encode again.
const int kTolerance = 12;

nlohmann::json fixtureNamed(const std::string &file) {
    for (const nlohmann::json &fixture : all()) {
        if (fixture.value("file", "") == file) {
            return fixture;
        }
    }
    return nlohmann::json();
}

// The picture as it is shown for an EXIF orientation: what a thumbnailer
// saves, worked out here apart from Bitmap::toStoredOrientation.
Bitmap uprightOf(const Bitmap &stored, int orientation) {
    const int w = stored.width();
    const int h = stored.height();
    const bool turned = orientation >= 5 && orientation <= 8;
    Bitmap upright(turned ? h : w, turned ? w : h, stored.order());
    for (int sy = 0; sy < h; ++sy) {
        for (int sx = 0; sx < w; ++sx) {
            int ux = sx;
            int uy = sy;
            switch (orientation) {
            case 2: ux = w - 1 - sx; break;
            case 3: ux = w - 1 - sx; uy = h - 1 - sy; break;
            case 4: uy = h - 1 - sy; break;
            case 5: ux = sy; uy = sx; break;
            case 6: ux = h - 1 - sy; uy = sx; break;
            case 7: ux = h - 1 - sy; uy = w - 1 - sx; break;
            case 8: ux = sy; uy = w - 1 - sx; break;
            default: break;
            }
            const uint8_t *from = stored.pixels() + ((size_t)sy * (size_t)w + (size_t)sx) * 4;
            uint8_t *to = upright.pixels() + ((size_t)uy * (size_t)upright.width() + (size_t)ux) * 4;
            std::copy(from, from + 4, to);
        }
    }
    if (stored.knownOpaque()) {
        upright.markOpaque();
    }
    return upright;
}

// Copies of fixtures in a folder of their own, and a freedesktop.org cache
// holding the upright thumbnail a file manager would have made of each, with
// XDG_CACHE_HOME pointed at it while the test runs. Windows asks the shell
// instead, which makes its own.
struct Library {
    Library() : root((fs::temp_directory_path() / "gallery3d_system_thumbnail").string()) {
        std::error_code error;
        fs::remove_all(root, error);
        fs::create_directories(fs::path(root) / "photos", error);
        const char *previous = SDL_getenv("XDG_CACHE_HOME");
        hadCache = previous != nullptr;
        if (hadCache) {
            previousCache = previous;
        }
        SDL_setenv_unsafe("XDG_CACHE_HOME", (fs::path(root) / "cache").string().c_str(), 1);
    }

    ~Library() {
        if (hadCache) {
            SDL_setenv_unsafe("XDG_CACHE_HOME", previousCache.c_str(), 1);
        } else {
            SDL_unsetenv_unsafe("XDG_CACHE_HOME");
        }
        std::error_code error;
        fs::remove_all(root, error);
    }

    // Copies the fixture in and files its upright thumbnail, long edge 256 as
    // a thumbnailer saves the large size. Returns the copy's path.
    std::string add(const std::string &file) {
        const fs::path copy = fs::path(root) / "photos" / file;
        std::error_code error;
        fs::copy_file(folder() + file, copy, fs::copy_options::overwrite_existing, error);
        const Bitmap whole = Bitmap::load(copy.string(), 0);
        const int longEdge = std::max(whole.width(), whole.height());
        const Bitmap stored =
            longEdge > 256 ? whole.scaled(whole.width() * 256 / longEdge, whole.height() * 256 / longEdge) : whole;
        const Bitmap upright = uprightOf(stored, Bitmap::readExif(copy.string()).orientation);
        CHECK_DETAIL(FreedesktopCache::put(ThumbnailCache::root(), copy.string(), "large", upright,
                                           FreedesktopCache::modifiedSeconds(copy.string())),
                     file + ": could not file a thumbnail");
        return copy.string();
    }

    std::string root;
    bool hadCache = false;
    std::string previousCache;
};

}  // namespace

TEST(a_system_thumbnail_comes_back_in_the_stored_orientation) {
    if (!SystemThumbnail::supported()) {
        SKIP("no system thumbnails on this platform");
    }
    Library library;
    for (int orientation = 1; orientation <= 8; ++orientation) {
        const std::string file = "orientation_" + std::to_string(orientation) + ".jpg";
        const nlohmann::json fixture = fixtureNamed(file);
        CHECK_DETAIL(fixture.is_object() && !fixture["scaled"].empty(), file + ": not in the manifest");
        if (!fixture.is_object() || fixture["scaled"].empty()) {
            continue;
        }
        const int maxEdge = fixture["scaled"][0]["maxEdge"];
        const std::string path = library.add(file);
        const Bitmap thumbnail = SystemThumbnail::load(path, maxEdge);
        CHECK_DETAIL(thumbnail.valid(), file + ": no thumbnail");
        if (!thumbnail.valid()) {
            continue;
        }
        // The store's size is its own, so only the reduction is certain: the
        // long edge reaches maxEdge and is under twice it, in the stored shape.
        const int width = thumbnail.width();
        const int height = thumbnail.height();
        CHECK_DETAIL(width >= maxEdge && width < maxEdge * 2 &&
                         std::abs(height * fixture["width"].get<int>() - width * fixture["height"].get<int>()) <=
                             fixture["width"].get<int>(),
                     file + ": thumbnail is " + std::to_string(width) + "x" + std::to_string(height));
        CHECK_DETAIL(thumbnail.order() == Bitmap::decodeOrder(), file + ": pixels not in decodeOrder()");
        CHECK_DETAIL(thumbnail.knownOpaque(), file + ": an opaque photo's thumbnail is not marked opaque");
        // Each patch's colour at the middle of its quarter, wherever the
        // thumbnail's size puts it.
        nlohmann::json probes = nlohmann::json::array();
        const int points[4][2] = {{width / 4, height / 4}, {3 * width / 4, height / 4},
                                  {width / 4, 3 * height / 4}, {3 * width / 4, 3 * height / 4}};
        for (int index = 0; index < 4; ++index) {
            const nlohmann::json &probe = fixture["probes"][index];
            probes.push_back({points[index][0], points[index][1], probe[2], probe[3], probe[4], probe[5]});
        }
        checkProbes(file, thumbnail, probes, kTolerance);
    }
}

TEST(a_system_thumbnail_is_never_enlarged) {
    if (!SystemThumbnail::supported()) {
        SKIP("no system thumbnails on this platform");
    }
    Library library;
    // The picture is 320 wide, and no thumbnail of it reaches 512.
    const std::string path = library.add("orientation_1.jpg");
    CHECK(SystemThumbnail::load(path, 160).valid());
    CHECK(!SystemThumbnail::load(path, 512).valid());
}

TEST(a_missing_file_or_no_size_has_no_system_thumbnail) {
    CHECK(!SystemThumbnail::load((fs::temp_directory_path() / "gallery3d_thumbnail_missing.jpg").string(), 160)
               .valid());
    CHECK(!SystemThumbnail::load(folder() + "orientation_1.jpg", 0).valid());
    CHECK(!SystemThumbnail::load(folder() + "orientation_1.jpg", -1).valid());
}
