// Stands in for com.cooliris.media.DiskCache: a keyed blob store for decoded
// thumbnails, so the second run of the app does not decode every original
// again. The original packed its records into 1MB chunk files with a separate
// index, because it ran on a phone filesystem. Here one file per entry is
// enough, and it makes a half written entry impossible to read: each entry is
// written to a temporary name and renamed into place.
//
// The store holds premultiplied RGBA exactly as Bitmap carries it, so a hit
// costs a read and a memcpy and never runs the pixels through premultiplication
// a second time.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "Bitmap.h"

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
