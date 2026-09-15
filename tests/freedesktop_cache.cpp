#include "freedesktop_cache.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/Md5.h"
#include "graphics/ThumbnailCache.h"

namespace fs = std::filesystem;

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

}  // namespace

namespace FreedesktopCache {

long long modifiedSeconds(const std::string &path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) {
        return 0;
    }
    return (long long)(info.modify_time / SDL_NS_PER_SECOND);
}

bool put(const std::string &root, const std::string &path, const std::string &folder, const Bitmap &thumbnail,
         long long mtime) {
    const fs::path directory = fs::path(root) / folder;
    std::error_code error;
    fs::create_directories(directory, error);
    const std::string encoded = (fs::path(root) / "encoded.png").string();
    std::vector<uint8_t> png;
    if (!thumbnail.savePng(encoded) || !Bitmap::readFile(encoded, &png) || png.size() < 33) {
        return false;
    }
    fs::remove(encoded, error);
    png = withText(png, "Thumb::URI", ThumbnailCache::uriFor(path));
    png = withText(png, "Thumb::MTime", std::to_string(mtime));
    std::ofstream out(directory / (Md5::hex(ThumbnailCache::uriFor(path)) + ".png"), std::ios::binary);
    out.write((const char *)png.data(), (std::streamsize)png.size());
    return (bool)out;
}

}  // namespace FreedesktopCache
