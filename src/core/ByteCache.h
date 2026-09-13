// Thread-safe, bounded LRU cache of encoded byte blobs, keyed by string.
// Shares downloads across texture sizes for the current session; DiskCache stores decoded
// pixels.
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

    // Replaces the key and evicts least-recently-used entries to fit. Oversized blobs are not
    // stored.
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
