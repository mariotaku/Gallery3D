// Port of com.cooliris.media.MediaBucket and MediaBucketList.
#pragma once

#include <map>
#include <memory>
#include <vector>

class MediaFeed;
class MediaItem;
class MediaSet;

struct MediaBucket {
    MediaSet *mediaSet = nullptr;
    // Empty means the whole set is selected, not the items inside it.
    std::vector<MediaItem *> mediaItems;
    bool hasItems = false;
};

class MediaBucketList {
  public:
    std::vector<MediaBucket> &get() {
        return mBuckets;
    }

    int size();
    void add(int slotId, MediaFeed *feed, bool removeIfAlreadyAdded);
    bool find(MediaItem *item);
    void clear();

  private:
    void setDirty() {
        mDirtyCount = true;
        mDirtyAcceleratedLookup = true;
    }

    std::vector<MediaBucket> mBuckets;
    std::map<MediaItem *, bool> mCachedItems;
    bool mDirtyCount = true;
    bool mDirtyAcceleratedLookup = true;
    int mCount = 0;
};
