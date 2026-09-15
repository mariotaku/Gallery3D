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

#include "media/MediaBucketList.h"
#include "media/MediaClustering.h"
#include "media/MediaSet.h"
#include "core/Shared.h"

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

    // Hands over a set its source has filled, items and title included. The
    // feed takes it in between frames, from pumpListener, replacing any set
    // with the same id. The draw reads sets without a lock, so a set must not
    // change while the draw can see it. A set with no source of its own gets
    // the feed's.
    void addMediaSet(std::unique_ptr<MediaSet> set);

    // A page of items for a set the feed already has, added between frames the
    // same way. then runs on the set once the items are in, to sort or retitle
    // it. The page is dropped if the set has left the feed by then.
    void addItems(MediaSet *set, std::vector<std::unique_ptr<MediaItem>> items,
                  std::function<void(MediaSet &)> then = nullptr);

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
    // While a hold is open the call is ignored, so a source that runs other sources
    // is the one that ends the load.
    void finishLoadingMediaSets();

    // Opens and closes a hold on finishLoadingMediaSets. Closing the last hold
    // finishes the load. Call both on the loader thread.
    void holdFinishLoadingMediaSets();
    void releaseFinishLoadingMediaSets();

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
    // Takes in what addMediaSet and addItems queued. Runs on the render thread,
    // between frames.
    void applyPendingChanges();

    // A set to take in, or a page of items for target.
    struct PendingChange {
        std::unique_ptr<MediaSet> set;
        MediaSet *target = nullptr;
        std::vector<std::unique_ptr<MediaItem>> items;
        std::function<void(MediaSet &)> then;
    };

    void loaderThread();
    // Hands a job to the loader thread. Ignored once shutdown has begun.
    void postJob(std::function<void()> job);
    // Runs on the loader thread, one item at a time.
    bool performOperationOnItem(int operation, MediaItem *item, const void *data);

    DataSource *mDataSource = nullptr;
    Listener *mListener = nullptr;

    std::vector<std::unique_ptr<MediaSet>> mMediaSets;
    std::mutex mSetsMutex;

    // Queued by sources on the loader thread, taken in by pumpListener.
    std::vector<PendingChange> mPendingChanges;
    std::mutex mPendingMutex;

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
    std::atomic<int> mFinishHolds{0};
    // Sets with a page in flight. The feed owns them, so pointers outlive requests.
    std::set<MediaSet *> mLoadsInFlight;
    std::mutex mInFlightMutex;

    // Makes shutdown idempotent.
    bool mShutDown = false;
    std::atomic<bool> mShuttingDown{false};
    std::atomic<bool> mListenerNeedsUpdate{false};
    std::atomic<bool> mListenerNeedsLayout{false};
};
