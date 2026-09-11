// Port of com.cooliris.media.MediaFeed. Slots represent albums when collapsed,
// individual items when expanded, and clusters in timeline mode.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
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

    // Adds an empty set, replacing any existing id. Records its owning source for
    // item loads and operations; null uses the feed's source.
    MediaSet *addMediaSet(int64_t setId, DataSource *source = nullptr);

    // Requests items from the set's source without waiting. Ask even when items
    // exist: only the source knows whether further pages remain.
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

    // The source signals completion of the initial page, including asynchronous fetches.
    void finishLoadingMediaSets();

    // Marks a set's page complete so another request can start.
    void finishLoadingItemsForSet(MediaSet *set);

    // Whether a page for this set is already on its way.
    bool isLoadingItemsForSet(MediaSet *set);

    // Whether all selected sources support the operation; an empty selection uses the feed source.
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

    // One loader thread drains slow jobs in order, avoiding concurrent source calls.
    std::deque<std::function<void()>> mJobs;
    std::mutex mJobMutex;
    std::condition_variable mJobCondition;

    // Successful deletions awaiting render-thread removal between frames;
    // removal on the worker would invalidate raw pointers held by drawing code.
    std::vector<MediaItem *> mDeletedItems;
    std::mutex mDeletedMutex;

    std::atomic<bool> mLoading{false};
    // Sets with a page in flight. The feed owns them, so pointers outlive requests.
    std::set<MediaSet *> mLoadsInFlight;
    std::mutex mInFlightMutex;

    // Makes shutdown idempotent.
    bool mShutDown = false;
    std::atomic<bool> mShuttingDown{false};
    std::atomic<bool> mListenerNeedsUpdate{false};
    std::atomic<bool> mListenerNeedsLayout{false};
};
