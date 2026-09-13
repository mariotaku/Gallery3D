#include "media/MediaBucketList.h"

#include <algorithm>

#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "core/Shared.h"

int MediaBucketList::size() {
    if (mDirtyCount) {
        int count = 0;
        for (const MediaBucket &bucket : mBuckets) {
            int numItems = 0;
            if (!bucket.hasItems && bucket.mediaSet != nullptr) {
                numItems = bucket.mediaSet->getNumItems();
                // The selection is the bucket itself, not its contents.
                if (numItems == 0) {
                    numItems = 1;
                }
            } else {
                numItems = (int)bucket.mediaItems.size();
            }
            count += numItems;
        }
        mCount = count;
        mDirtyCount = false;
    }
    return mCount;
}

void MediaBucketList::add(int slotId, MediaFeed *feed, bool removeIfAlreadyAdded) {
    if (slotId == Shared::INVALID || feed == nullptr) {
        return;
    }
    setDirty();

    MediaSet *mediaSetToAdd = nullptr;
    const bool hasExpandedMediaSet = feed->hasExpandedMediaSet();
    if (!hasExpandedMediaSet) {
        std::vector<MediaSet *> mediaSets = feed->getMediaSets();
        if (slotId >= (int)mediaSets.size()) {
            return;
        }
        mediaSetToAdd = mediaSets[(size_t)slotId];
    } else {
        MediaSet *set = feed->getSetForSlot(slotId);
        if (set != nullptr && set->getNumItems() > 0) {
            mediaSetToAdd = set->getItems()[0]->mParentMediaSet;
        }
    }

    MediaBucket *bucket = nullptr;
    for (size_t i = 0; i < mBuckets.size(); ++i) {
        MediaBucket &candidate = mBuckets[i];
        // Compare reference identity so distinct sets sharing an id remain separate.
        if (candidate.mediaSet != nullptr && mediaSetToAdd != nullptr && candidate.mediaSet == mediaSetToAdd) {
            if (!hasExpandedMediaSet) {
                if (removeIfAlreadyAdded) {
                    mBuckets.erase(mBuckets.begin() + (long)i);
                }
                return;
            }
            bucket = &candidate;
            break;
        }
    }
    if (bucket == nullptr) {
        MediaBucket newBucket;
        newBucket.mediaSet = mediaSetToAdd;
        mBuckets.push_back(newBucket);
        bucket = &mBuckets.back();
    }

    if (hasExpandedMediaSet) {
        MediaSet *set = feed->getSetForSlot(slotId);
        if (set != nullptr) {
            bucket->hasItems = true;
            for (MediaItem *item : set->getItems()) {
                auto found = std::find(bucket->mediaItems.begin(), bucket->mediaItems.end(), item);
                if (found != bucket->mediaItems.end()) {
                    if (removeIfAlreadyAdded) {
                        bucket->mediaItems.erase(found);
                    }
                } else {
                    bucket->mediaItems.push_back(item);
                }
            }
        }
    }
    setDirty();
}

bool MediaBucketList::find(MediaItem *item) {
    if (mDirtyAcceleratedLookup) {
        mCachedItems.clear();
        mDirtyAcceleratedLookup = false;
    }
    auto cached = mCachedItems.find(item);
    if (cached != mCachedItems.end()) {
        return cached->second;
    }
    for (const MediaBucket &bucket : mBuckets) {
        if (!bucket.hasItems) {
            if (item->mParentMediaSet != nullptr && item->mParentMediaSet == bucket.mediaSet) {
                mCachedItems[item] = true;
                return true;
            }
        } else if (std::find(bucket.mediaItems.begin(), bucket.mediaItems.end(), item) != bucket.mediaItems.end()) {
            mCachedItems[item] = true;
            return true;
        }
    }
    mCachedItems[item] = false;
    return false;
}

void MediaBucketList::clear() {
    mBuckets.clear();
    setDirty();
}
