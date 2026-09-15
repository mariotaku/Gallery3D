#include "graphics/ThumbnailCache.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Md5.h"

namespace {

// The size folders, smallest first, with the edge their thumbnails fit in.
struct SizeFolder {
    const char *name;
    int edge;
};

const SizeFolder kSizeFolders[] = {{"normal", 128}, {"large", 256}, {"x-large", 512}, {"xx-large", 1024}};

// What g_escape_uri_string leaves as it is in a path: ASCII letters and digits
// and !$&'()*+,-./:=@_~. Every other byte is written as %XX.
bool keptInPath(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return c != '\0' && std::strchr("!$&'()*+,-./:=@_~", c) != nullptr;
}

uint32_t bigEndian32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

// The value of the PNG's tEXt chunk under key, or empty when it has none. The
// thumbnailers write their Thumb:: keys as tEXt.
std::string pngText(const std::vector<uint8_t> &png, const std::string &key) {
    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (png.size() < 8 || std::memcmp(png.data(), kSignature, 8) != 0) {
        return std::string();
    }
    size_t at = 8;
    // Length, type, data and CRC. A text chunk may come before or after the
    // image data, so the walk goes on to the end.
    while (at + 12 <= png.size()) {
        const uint32_t length = bigEndian32(&png[at]);
        if (length > png.size() - at - 12) {
            break;
        }
        const char *type = (const char *)&png[at + 4];
        if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        if (std::memcmp(type, "tEXt", 4) == 0) {
            const char *data = (const char *)&png[at + 8];
            const char *end = data + length;
            const char *separator = std::find(data, end, '\0');
            if (separator != end && std::string(data, separator) == key) {
                return std::string(separator + 1, end);
            }
        }
        at += 12 + (size_t)length;
    }
    return std::string();
}

}  // namespace

std::string ThumbnailCache::root() {
    const char *cache = SDL_getenv("XDG_CACHE_HOME");
    if (cache != nullptr && cache[0] != '\0') {
        return std::string(cache) + "/thumbnails";
    }
    const char *home = SDL_getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.cache/thumbnails";
    }
    return std::string();
}

std::string ThumbnailCache::uriFor(const std::string &path) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string uri = "file://";
    if (path.empty() || path[0] != '/') {
        uri += '/';
    }
    for (unsigned char c : path) {
        if (keptInPath(c)) {
            uri += (char)c;
        } else {
            uri += '%';
            uri += kHex[c >> 4];
            uri += kHex[c & 0x0F];
        }
    }
    return uri;
}

Bitmap ThumbnailCache::loadUpright(const std::string &root, const std::string &path, int maxEdge) {
    SDL_PathInfo info;
    if (root.empty() || path.empty() || maxEdge <= 0 || !SDL_GetPathInfo(path.c_str(), &info) ||
        info.type != SDL_PATHTYPE_FILE) {
        return Bitmap();
    }
    const long long modified = (long long)(info.modify_time / SDL_NS_PER_SECOND);
    const std::string name = Md5::hex(uriFor(path)) + ".png";

    // The smallest size that can reach maxEdge first. A picture smaller than a
    // size is kept at its own size, so a folder can hold one that falls short.
    for (const SizeFolder &folder : kSizeFolders) {
        if (folder.edge < maxEdge) {
            continue;
        }
        std::vector<uint8_t> png;
        if (!Bitmap::readFile(root + "/" + folder.name + "/" + name, &png)) {
            continue;
        }
        // The specification's check: a thumbnail with no Thumb::MTime, or one
        // other than the photo's, is out of date.
        const std::string mtime = pngText(png, "Thumb::MTime");
        if (mtime.empty() || std::strtoll(mtime.c_str(), nullptr, 10) != modified) {
            continue;
        }
        Bitmap upright = Bitmap::loadFromMemory(png.data(), png.size(), 0);
        if (!upright.valid() || std::max(upright.width(), upright.height()) < maxEdge) {
            continue;
        }
        upright.markOpaqueUnlessTransparent();
        return upright.sampledFromWhole(Sampling::Picked,
                                        Bitmap::sampleSizeFor(upright.width(), upright.height(), maxEdge));
    }
    return Bitmap();
}

