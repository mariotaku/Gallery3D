// A bounded, least-recently-used store of byte blobs, keyed by string.
//
// For a data source that fetches over the network. One picture is read three
// times - a thumbnail, a screennail, and the full resolution view when it is
// zoomed - and without something like this each read is its own download.
//
// Bounded because the obvious version is not. A map that only ever grows holds
// every image the session has touched: a hundred artworks an album, a couple of
// hundred kilobytes each, and twelve albums on the wall.
//
// This keeps the encoded bytes as they arrived. Decoded thumbnails are
// DiskCache's job and survive a restart; this only saves the download, and only
// while the app is up.
//
// Safe to use from several loader threads at once.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ByteCache {
  public:
    explicit ByteCache(size_t maxBytes) : mMaxBytes(maxBytes) {}

    // Copies the entry into `out` and makes it the most recently used. False on
    // a miss, with `out` untouched.
    bool get(const std::string &key, std::vector<uint8_t> *out);

    // Replaces any entry under the same key, then evicts from the least
    // recently used end until the store fits again.
    //
    // A blob too big for the budget is not stored at all. Keeping it would mean
    // throwing out everything else to hold one entry, which is worse than not
    // caching it.
    void put(const std::string &key, std::vector<uint8_t> bytes);

    size_t bytes() const;
    size_t count() const;
    void clear();

  private:
    struct Entry {
        std::string key;
        std::vector<uint8_t> bytes;
    };

    // Most recently used at the front. Call with mMutex held.
    void evictLocked();

    const size_t mMaxBytes;

    mutable std::mutex mMutex;
    std::list<Entry> mEntries;
    std::unordered_map<std::string, std::list<Entry>::iterator> mIndex;
    size_t mBytes = 0;
};
