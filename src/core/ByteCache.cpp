#include "core/ByteCache.h"

bool ByteCache::get(const std::string &key, std::vector<uint8_t> *out) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto found = mIndex.find(key);
    if (found == mIndex.end()) {
        return false;
    }
    // To the front, so the next eviction takes something else. splice moves the
    // node rather than copying the blob.
    mEntries.splice(mEntries.begin(), mEntries, found->second);
    if (out != nullptr) {
        *out = found->second->bytes;
    }
    return true;
}

void ByteCache::put(const std::string &key, std::vector<uint8_t> bytes) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto found = mIndex.find(key);
    if (found != mIndex.end()) {
        mBytes -= found->second->bytes.size();
        mEntries.erase(found->second);
        mIndex.erase(found);
    }

    const size_t size = bytes.size();
    if (size == 0 || size > mMaxBytes) {
        return;
    }

    mEntries.push_front(Entry{key, std::move(bytes)});
    mIndex[key] = mEntries.begin();
    mBytes += size;
    evictLocked();
}

void ByteCache::evictLocked() {
    while (mBytes > mMaxBytes && !mEntries.empty()) {
        const Entry &oldest = mEntries.back();
        mBytes -= oldest.bytes.size();
        mIndex.erase(oldest.key);
        mEntries.pop_back();
    }
}

size_t ByteCache::bytes() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mBytes;
}

size_t ByteCache::count() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mEntries.size();
}

void ByteCache::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mEntries.clear();
    mIndex.clear();
    mBytes = 0;
}
