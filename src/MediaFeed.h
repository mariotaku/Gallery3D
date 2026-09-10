// Port of com.cooliris.media.MediaFeed.
//
// Slot model, unchanged from the original:
//   no expanded set  -> one slot per album, each showing a stack of its items
//   expanded set     -> one slot per item, wrapped in a single item MediaSet
//   clustering mode  -> one slot per cluster
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MediaBucketList.h"
#include "MediaClustering.h"
#include "MediaSet.h"
#include "Shared.h"

class DataSource;

class MediaFeed {
  public:
    static const int OPERATION_DELETE = 0;
    static const int OPERATION_ROTATE = 1;
    static const int OPERATION_CROP = 2;

    class Listener {
      public:
        virtual ~Listener() = default;
        virtual void onFeedAboutToChange(MediaFeed *feed) = 0;
        virtual void onFeedChanged(MediaFeed *feed, bool needsLayout) = 0;
    };

    MediaFeed(DataSource *dataSource, Listener *listener);
    ~MediaFeed();

    void start();
    void shutdown();

    // Called once a frame from the render thread. Publishes work the loader
    // thread finished, so listeners only ever hear about it from one thread.
    void pumpListener();

    DataSource *getDataSource() const {
        return mDataSource;
    }

    std::vector<MediaSet *> getMediaSets();
    MediaSet *getFilteredSet() const {
        return nullptr;
    }

    // Appends an empty set with the given id and returns it. If a set with
    // that id is already present it is dropped first, so a rescan replaces a
    // stale album instead of duplicating it.
    MediaSet *addMediaSet(int64_t setId);

    int getNumSlots();
    MediaSet *getSetForSlot(int slotIndex);
    void setVisibleRange(int begin, int end);

    void expandMediaSet(int mediaSetIndex);
    bool canExpandSet(int slotIndex);
    bool hasExpandedMediaSet() const;
    MediaSet *getExpandedMediaSet();
    MediaSet *getCurrentSet();

    bool isClustered() const {
        return mInClusteringMode;
    }
    void performClustering();
    bool restorePreviousClusteringState();

    void copySlotStateFrom(const MediaFeed &another);
    void updateListener(bool needsLayout);

    bool getWaitingForMediaScanner() const {
        return false;
    }

    bool isLoading() const {
        return mLoading.load();
    }

    bool isSingleImageMode() const {
        return mSingleImageMode;
    }

    void setSingleImageMode(bool singleImageMode) {
        mSingleImageMode = singleImageMode;
    }

    // Deletes the selection to the recycle bin, or rotates it by data degrees.
    void performOperation(int operation, std::vector<MediaBucket> *mediaBuckets, const void *data);
    void setFilter(void *filter);
    void removeFilter();
    const std::vector<int> *getBreaks() const {
        return nullptr;
    }

    void onResume() {}
    void onPause() {}

    // Drops an item from whichever set owns it. Used after a delete.
    void removeItem(MediaItem *item);

  private:
    void loaderThread();

    DataSource *mDataSource = nullptr;
    Listener *mListener = nullptr;

    std::vector<std::unique_ptr<MediaSet>> mMediaSets;
    std::mutex mSetsMutex;

    int mExpandedMediaSetIndex = Shared::INVALID;
    bool mInClusteringMode = false;
    bool mSingleImageMode = false;

    // Wraps one item so a grid slot can be handed a MediaSet, as the original did.
    MediaSet mSingleWrapper;

    // Rebuilt whenever the timeline is entered. Owns the cluster sets, which
    // reference items the album still owns.
    MediaClustering mClustering;

    std::thread mLoaderThread;
    std::atomic<bool> mLoading{false};
    std::atomic<bool> mShuttingDown{false};
    std::atomic<bool> mListenerNeedsUpdate{false};
    std::atomic<bool> mListenerNeedsLayout{false};
};
