// Port of com.cooliris.media.IndexRange and Pool.
#pragma once

#include <vector>

struct IndexRange {
    int begin = 0;
    int end = 0;

    IndexRange() = default;

    IndexRange(int beginRange, int endRange) : begin(beginRange), end(endRange) {}

    void set(int b, int e) {
        begin = b;
        end = e;
    }

    bool isEmpty() const {
        return begin == end;
    }

    int size() const {
        return end - begin;
    }
};

// Fixed free list for pooled objects, matching the Java call sites.
template <typename T>
class Pool {
  public:
    explicit Pool(int count) : mObjects((size_t)count), mFreeList((size_t)count), mFreeListIndex(count) {
        for (int i = 0; i < count; ++i) {
            mFreeList[(size_t)i] = &mObjects[(size_t)i];
        }
    }

    T *create() {
        int index = --mFreeListIndex;
        if (index >= 0 && index < (int)mFreeList.size()) {
            T *object = mFreeList[(size_t)index];
            mFreeList[(size_t)index] = nullptr;
            return object;
        }
        mFreeListIndex = 0;
        return nullptr;
    }

    void destroy(T *object) {
        int index = mFreeListIndex;
        if (index >= 0 && index < (int)mFreeList.size()) {
            mFreeList[(size_t)index] = object;
            ++mFreeListIndex;
        }
    }

  private:
    std::vector<T> mObjects;
    std::vector<T *> mFreeList;
    int mFreeListIndex;
};
