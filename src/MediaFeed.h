// Port of com.cooliris.media.MediaFeed.
//
// Slot model, unchanged from the original:
//   no expanded set  -> one slot per album, each showing a stack of its items
//   expanded set     -> one slot per item, wrapped in a single item MediaSet
//   clustering mode  -> one slot per cluster
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
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
    //
    // The source is remembered on the set. Pass the one doing the adding; it
    // is what the feed calls back for that set's items and operations. Null
    // means the feed's own source, which is the single source case.
    MediaSet *addMediaSet(int64_t setId, DataSource *source = nullptr);

    // Asks whoever created the set for its items, on the loader thread. Returns
    // at once: the set fills in later and the listener hears about it, which is
    // the same shape the first scan already has. A set that has its items
    // already is not queued at all, so a source that loads everything up front
    // costs nothing here.
    void loadItemsForSet(MediaSet *set);

    // True once shutdown has begun. A data source doing slow work should poll
    // this and give up: without it, quitting waits for the network.
    bool isCancelled() const {
        return mShuttingDown.load();
    }

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

    // Whether every source behind the selection can carry the operation out.
    // False for an empty selection, and false if even one item's source cannot,
    // since a partial delete is worse than none. The HUD asks before offering
    // the button.
    bool selectionSupports(int operation, const std::vector<MediaBucket> *mediaBuckets) const;
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
    // Hands a job to the loader thread. Ignored once shutdown has begun.
    void postJob(std::function<void()> job);
    // Runs on the loader thread, one item at a time.
    bool performOperationOnItem(int operation, MediaItem *item, const void *data);

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

    // Everything slow happens here. The queue is drained in order by the one
    // loader thread, so a source never sees two calls at once and does not
    // have to be thread safe with itself.
    std::deque<std::function<void()>> mJobs;
    std::mutex mJobMutex;
    std::condition_variable mJobCondition;

    // Items whose delete went through, waiting for the render thread to drop
    // them. The structural change cannot happen on the loader thread: the draw
    // code holds raw MediaItem pointers across frames, so removing one
    // underneath it would leave those dangling.
    std::vector<MediaItem *> mDeletedItems;
    std::mutex mDeletedMutex;

    std::atomic<bool> mLoading{false};
    std::atomic<bool> mShuttingDown{false};
    std::atomic<bool> mListenerNeedsUpdate{false};
    std::atomic<bool> mListenerNeedsLayout{false};
};
