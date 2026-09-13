// Stands in for com.cooliris.media.DiskCache: decoded premultiplied RGBA thumbnails.
// Entries are written to temporary files and renamed into place atomically.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"

class DiskCache {
  public:
    // The shared thumbnail store, under SDL_GetPrefPath. Never inside the
    // photo directory.
    static DiskCache &thumbnails();

    // Returns an invalid bitmap on a miss. Safe to call from several loader
    // threads at once.
    Bitmap get(const std::string &key);

    // Replaces any entry under the same key. Safe to call from several loader
    // threads at once.
    void put(const std::string &key, const Bitmap &bitmap);

  private:
    struct Entry {
        std::string name;
        uint64_t size = 0;
        int64_t writeTime = 0;
    };

    DiskCache(const std::string &directory, uint64_t maxBytes);

    std::string pathFor(const std::string &key) const;
    // Drops the oldest entries until the store fits the cap again. Call with
    // mMutex held.
    void evictLocked();

    std::string mDirectory;
    uint64_t mMaxBytes = 0;
    bool mUsable = false;

    std::mutex mMutex;
    std::vector<Entry> mEntries;
    uint64_t mTotalBytes = 0;
    uint64_t mTempCounter = 0;
};
