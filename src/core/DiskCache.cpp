#include "core/DiskCache.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace {

// "G3DT", so a file left over from some other tool is rejected rather than
// read as pixels.
const uint32_t kMagic = 0x54443347u;
// Bump this whenever the layout below changes, so old entries are dropped
// instead of misread.
const uint32_t kVersion = 1u;
const uint64_t kMaxBytes = 256ull * 1024ull * 1024ull;
// Evicting down to the cap alone would make every following put evict again.
// Free a tenth of the store instead.
const double kEvictTo = 0.9;

const char *const kEntrySuffix = ".thumb";
const char *const kTempPrefix = "tmp_";

struct Header {
    uint32_t magic;
    uint32_t version;
    int32_t width;
    int32_t height;
    uint32_t keyLength;
};

// The header goes to disk as raw bytes, so it must not grow padding.
static_assert(sizeof(Header) == 20, "DiskCache header layout changed");

uint64_t hashKey(const std::string &key) {
    // FNV-1a. The full key goes into the file as well, so a collision is
    // caught on read rather than serving the wrong thumbnail.
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : key) {
        hash ^= (uint64_t)c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string toHex(uint64_t value) {
    char buffer[17];
    SDL_snprintf(buffer, sizeof(buffer), "%016llx", (unsigned long long)value);
    return std::string(buffer);
}

bool readFully(SDL_IOStream *stream, void *data, size_t size) {
    return SDL_ReadIO(stream, data, size) == size;
}

bool writeFully(SDL_IOStream *stream, const void *data, size_t size) {
    return SDL_WriteIO(stream, data, size) == size;
}

bool hasSuffix(const std::string &name, const char *suffix) {
    size_t length = SDL_strlen(suffix);
    return name.size() > length && name.compare(name.size() - length, length, suffix) == 0;
}

}  // namespace

DiskCache &DiskCache::thumbnails() {
    static DiskCache *instance = [] {
        std::string directory;
        char *prefPath = SDL_GetPrefPath("gallery3d-sdl", "thumbnails");
        if (prefPath) {
            directory = prefPath;
            SDL_free(prefPath);
        }
        return new DiskCache(directory, kMaxBytes);
    }();
    return *instance;
}

DiskCache::DiskCache(const std::string &directory, uint64_t maxBytes)
    : mDirectory(directory), mMaxBytes(maxBytes) {
    if (mDirectory.empty()) {
        SDL_Log("DiskCache: no writable pref path, thumbnails will be decoded every run");
        return;
    }
    mUsable = true;

    // Take stock of what a previous run left behind, so eviction has something
    // to work with and stale temporary files do not pile up.
    SDL_EnumerateDirectory(
        mDirectory.c_str(),
        [](void *userdata, const char *dirname, const char *fname) {
            (void)dirname;
            DiskCache *self = (DiskCache *)userdata;
            std::string name = fname;
            std::string path = self->mDirectory + name;
            if (name.compare(0, SDL_strlen(kTempPrefix), kTempPrefix) == 0) {
                SDL_RemovePath(path.c_str());
                return SDL_ENUM_CONTINUE;
            }
            if (!hasSuffix(name, kEntrySuffix)) {
                return SDL_ENUM_CONTINUE;
            }
            SDL_PathInfo info;
            if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
                Entry entry;
                entry.name = name;
                entry.size = (uint64_t)info.size;
                entry.writeTime = (int64_t)info.modify_time;
                self->mTotalBytes += entry.size;
                self->mEntries.push_back(entry);
            }
            return SDL_ENUM_CONTINUE;
        },
        this);

    std::lock_guard<std::mutex> lock(mMutex);
    evictLocked();
}

std::string DiskCache::pathFor(const std::string &key) const {
    return mDirectory + toHex(hashKey(key)) + kEntrySuffix;
}

Bitmap DiskCache::get(const std::string &key) {
    if (!mUsable) {
        return Bitmap();
    }
    // No lock here. Entries are only ever renamed into place whole, so a
    // reader either sees the previous file or the new one, never a mix.
    SDL_IOStream *stream = SDL_IOFromFile(pathFor(key).c_str(), "rb");
    if (!stream) {
        return Bitmap();
    }

    Bitmap result;
    Header header;
    std::string storedKey;
    if (readFully(stream, &header, sizeof(header)) && header.magic == kMagic &&
        header.version == kVersion && header.width > 0 && header.height > 0 &&
        header.keyLength == key.size()) {
        storedKey.resize(header.keyLength);
        if (readFully(stream, &storedKey[0], storedKey.size()) && storedKey == key) {
            Bitmap bitmap(header.width, header.height);
            size_t bytes = (size_t)header.width * (size_t)header.height * 4;
            if (bitmap.valid() && readFully(stream, bitmap.pixels(), bytes)) {
                result = std::move(bitmap);
            }
        }
    }
    SDL_CloseIO(stream);
    return result;
}

void DiskCache::put(const std::string &key, const Bitmap &bitmap) {
    if (!mUsable || !bitmap.valid()) {
        return;
    }

    std::string tempPath;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        tempPath = mDirectory + kTempPrefix + toHex(mTempCounter++) + toHex(hashKey(key));
    }

    SDL_IOStream *stream = SDL_IOFromFile(tempPath.c_str(), "wb");
    if (!stream) {
        return;
    }

    Header header;
    header.magic = kMagic;
    header.version = kVersion;
    header.width = bitmap.width();
    header.height = bitmap.height();
    header.keyLength = (uint32_t)key.size();
    size_t pixelBytes = (size_t)bitmap.width() * (size_t)bitmap.height() * 4;
    bool written = writeFully(stream, &header, sizeof(header)) &&
                   writeFully(stream, key.data(), key.size()) &&
                   writeFully(stream, bitmap.pixels(), pixelBytes);
    if (!SDL_CloseIO(stream)) {
        written = false;
    }
    if (!written) {
        SDL_RemovePath(tempPath.c_str());
        return;
    }

    std::string path = pathFor(key);
    if (!SDL_RenamePath(tempPath.c_str(), path.c_str())) {
        // Some platforms refuse to rename onto an existing file. Clearing the
        // way leaves a window where a reader misses, which only costs a decode.
        SDL_RemovePath(path.c_str());
        if (!SDL_RenamePath(tempPath.c_str(), path.c_str())) {
            SDL_RemovePath(tempPath.c_str());
            return;
        }
    }

    uint64_t size = sizeof(Header) + key.size() + pixelBytes;
    std::lock_guard<std::mutex> lock(mMutex);
    std::string name = path.substr(mDirectory.size());
    auto existing = std::find_if(mEntries.begin(), mEntries.end(),
                                 [&](const Entry &entry) { return entry.name == name; });
    if (existing != mEntries.end()) {
        mTotalBytes -= existing->size;
        mEntries.erase(existing);
    }
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    Entry entry;
    entry.name = name;
    entry.size = size;
    entry.writeTime = (int64_t)now;
    mEntries.push_back(entry);
    mTotalBytes += size;
    evictLocked();
}

void DiskCache::evictLocked() {
    if (mTotalBytes <= mMaxBytes) {
        return;
    }
    // Oldest first. Write time, not access time, so this does not have to touch
    // every file it reads.
    std::sort(mEntries.begin(), mEntries.end(),
              [](const Entry &a, const Entry &b) { return a.writeTime < b.writeTime; });

    uint64_t target = (uint64_t)(mMaxBytes * kEvictTo);
    size_t dropped = 0;
    while (dropped < mEntries.size() && mTotalBytes > target) {
        const Entry &entry = mEntries[dropped];
        // A failed remove means another process still has the file open. Drop
        // the accounting anyway; the next run picks the file up again.
        SDL_RemovePath((mDirectory + entry.name).c_str());
        mTotalBytes -= std::min(mTotalBytes, entry.size);
        ++dropped;
    }
    mEntries.erase(mEntries.begin(), mEntries.begin() + dropped);
}
