#include "MediaFeed.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>

#include "FileOperations.h"
#include "LocalDataSource.h"

MediaFeed::MediaFeed(DataSource *dataSource, Listener *listener) : mDataSource(dataSource), mListener(listener) {}

MediaSet *MediaFeed::addMediaSet(int64_t setId, DataSource *source) {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    for (size_t i = 0; i < mMediaSets.size(); ++i) {
        if (mMediaSets[i]->mId == setId) {
            // Replace an existing set with the same id during rescans.
            mMediaSets.erase(mMediaSets.begin() + (long)i);
            break;
        }
    }
    mMediaSets.push_back(std::make_unique<MediaSet>());
    mMediaSets.back()->mId = setId;
    mMediaSets.back()->mDataSource = (source != nullptr) ? source : mDataSource;
    return mMediaSets.back().get();
}

void MediaFeed::loadItemsForSet(MediaSet *set) {
    if (set == nullptr) {
        return;
    }
    // Ask the source even when covers are loaded; only it knows whether more pages remain.
    DataSource *source = (set->mDataSource != nullptr) ? set->mDataSource : mDataSource;
    if (source == nullptr) {
        return;
    }
    {
        // Allow one pending page request per set.
        std::lock_guard<std::mutex> lock(mInFlightMutex);
        if (!mLoadsInFlight.insert(set).second) {
            return;
        }
    }
    postJob([this, source, set]() {
        if (mShuttingDown.load()) {
            finishLoadingItemsForSet(set);
            return;
        }
        source->loadItemsForSet(this, set);
        updateListener(true);
    });
}

void MediaFeed::finishLoadingItemsForSet(MediaSet *set) {
    std::lock_guard<std::mutex> lock(mInFlightMutex);
    mLoadsInFlight.erase(set);
}

bool MediaFeed::isLoadingItemsForSet(MediaSet *set) {
    std::lock_guard<std::mutex> lock(mInFlightMutex);
    return mLoadsInFlight.count(set) != 0;
}

MediaFeed::~MediaFeed() {
    shutdown();
}

void MediaFeed::start() {
    mLoading.store(true);
#if defined(__EMSCRIPTEN__)
    // Browser sources complete through callbacks; workers require shared-memory response
    // headers.
    if (mDataSource) {
        mDataSource->loadMediaSets(this);
    }
#else
    if (mLoaderThread.joinable()) {
        return;
    }
    mLoaderThread = std::thread([this]() { loaderThread(); });
#endif
}

void MediaFeed::shutdown() {
    // Called twice: once on the way out of main, and again from the destructor.
    // The second time the source may already be gone, so do the work once.
    if (mShutDown) {
        return;
    }
    mShutDown = true;

    // Set before waking anyone, so a source polling isCancelled sees it and a
    // slow fetch does not hold the quit up.
    mShuttingDown.store(true);
    mJobCondition.notify_all();
    if (mLoaderThread.joinable()) {
        mLoaderThread.join();
    }
    if (mDataSource) {
        mDataSource->shutdown();
    }
}

void MediaFeed::finishLoadingMediaSets() {
    mLoading.store(false);
    updateListener(true);
}

void MediaFeed::loaderThread() {
    if (mDataSource) {
        mDataSource->loadMediaSets(this);
    }

    // Then stay alive for everything else slow: a set's items, a delete, a
    // rotation. One thread draining in order, so a source is never re-entered.
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mJobMutex);
            mJobCondition.wait(lock, [this]() { return !mJobs.empty() || mShuttingDown.load(); });
            if (mShuttingDown.load()) {
                return;
            }
            job = std::move(mJobs.front());
            mJobs.pop_front();
        }
        job();
    }
}

void MediaFeed::postJob(std::function<void()> job) {
#if defined(__EMSCRIPTEN__)
    // Run callback-based jobs inline; their slow work completes asynchronously.
    if (!mShuttingDown.load()) {
        job();
    }
    return;
#else
    if (mShuttingDown.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mJobMutex);
        mJobs.push_back(std::move(job));
    }
    mJobCondition.notify_one();
#endif
}

void MediaFeed::pumpListener() {
    // Deletions confirmed by the worker are applied here, between frames, so
    // nothing the draw code is holding disappears mid frame.
    {
        std::vector<MediaItem *> deleted;
        {
            std::lock_guard<std::mutex> lock(mDeletedMutex);
            deleted.swap(mDeletedItems);
        }
        if (!deleted.empty()) {
            if (mListener) {
                mListener->onFeedAboutToChange(this);
            }
            for (MediaItem *item : deleted) {
                removeItem(item);
            }
        }
    }

    if (!mListenerNeedsUpdate.exchange(false)) {
        return;
    }
    bool needsLayout = mListenerNeedsLayout.exchange(false);
    if (mListener) {
        mListener->onFeedChanged(this, needsLayout);
    }
}

void MediaFeed::updateListener(bool needsLayout) {
    mListenerNeedsUpdate.store(true);
    if (needsLayout) {
        mListenerNeedsLayout.store(true);
    }
}

std::vector<MediaSet *> MediaFeed::getMediaSets() {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    std::vector<MediaSet *> sets;
    sets.reserve(mMediaSets.size());
    for (const std::unique_ptr<MediaSet> &set : mMediaSets) {
        sets.push_back(set.get());
    }
    return sets;
}

int MediaFeed::getNumSlots() {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    int mediaSetsSize = (int)mMediaSets.size();
    int currentMediaSetIndex = mExpandedMediaSetIndex;

    if (!mInClusteringMode) {
        if (currentMediaSetIndex == Shared::INVALID || currentMediaSetIndex >= mediaSetsSize) {
            return mediaSetsSize;
        }
        return mMediaSets[(size_t)currentMediaSetIndex]->getNumExpectedItems();
    }
    return (int)mClustering.getClustersForDisplay().size();
}

MediaSet *MediaFeed::getSetForSlot(int slotIndex) {
    if (slotIndex < 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mSetsMutex);
    int mediaSetsSize = (int)mMediaSets.size();
    int currentMediaSetIndex = mExpandedMediaSetIndex;

    if (!mInClusteringMode) {
        if (currentMediaSetIndex == Shared::INVALID || currentMediaSetIndex >= mediaSetsSize) {
            if (slotIndex >= mediaSetsSize) {
                return nullptr;
            }
            return mMediaSets[(size_t)slotIndex].get();
        }
        MediaSet *setToUse = mMediaSets[(size_t)currentMediaSetIndex].get();
        if (slotIndex >= setToUse->getNumItems()) {
            return nullptr;
        }
        mSingleWrapper.getItems().assign(1, setToUse->getItems()[(size_t)slotIndex]);
        mSingleWrapper.mName = setToUse->mName;
        mSingleWrapper.mId = setToUse->mId;
        mSingleWrapper.setNumExpectedItems(1);
        return &mSingleWrapper;
    }
    std::vector<std::unique_ptr<MediaSet>> &clusters = mClustering.getClustersForDisplay();
    if (slotIndex < (int)clusters.size()) {
        return clusters[(size_t)slotIndex].get();
    }
    return nullptr;
}

void MediaFeed::setVisibleRange(int begin, int end) {
    (void)begin;
    (void)end;
    // Local sets load eagerly; no background-loading priority is needed.
}

void MediaFeed::expandMediaSet(int mediaSetIndex) {
    if (mListener) {
        mListener->onFeedAboutToChange(this);
    }
    // A source that fills its sets lazily is asked here, but not waited for.
    // This runs on the render thread, and the album opens on whatever is there
    // now; the items arrive later and the listener rebuilds the slots.
    MediaSet *setToFill = nullptr;
    {
        std::lock_guard<std::mutex> lock(mSetsMutex);
        if (mediaSetIndex >= 0 && mediaSetIndex < (int)mMediaSets.size()) {
            setToFill = mMediaSets[(size_t)mediaSetIndex].get();
        }
    }
    loadItemsForSet(setToFill);
    {
        std::lock_guard<std::mutex> lock(mSetsMutex);
        mExpandedMediaSetIndex = mediaSetIndex;
    }
    updateListener(true);
}

bool MediaFeed::canExpandSet(int slotIndex) {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    if (slotIndex >= 0 && slotIndex < (int)mMediaSets.size()) {
        MediaSet *set = mMediaSets[(size_t)slotIndex].get();
        if (set->getNumItems() > 0) {
            return set->getItems()[0]->mId != Shared::INVALID;
        }
    }
    return false;
}

bool MediaFeed::hasExpandedMediaSet() const {
    return mExpandedMediaSetIndex != Shared::INVALID;
}

MediaSet *MediaFeed::getExpandedMediaSet() {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    if (mExpandedMediaSetIndex == Shared::INVALID || mExpandedMediaSetIndex >= (int)mMediaSets.size()) {
        return nullptr;
    }
    return mMediaSets[(size_t)mExpandedMediaSetIndex].get();
}

MediaSet *MediaFeed::getCurrentSet() {
    return getExpandedMediaSet();
}

void MediaFeed::performClustering() {
    if (mListener) {
        mListener->onFeedAboutToChange(this);
    }

    std::lock_guard<std::mutex> lock(mSetsMutex);
    if (mExpandedMediaSetIndex == Shared::INVALID || mExpandedMediaSetIndex >= (int)mMediaSets.size()) {
        return;
    }
    MediaSet *setToUse = mMediaSets[(size_t)mExpandedMediaSetIndex].get();

    // Sort oldest first; clustering compares adjacent items in time order.
    std::vector<MediaItem *> items = setToUse->getItems();
    std::stable_sort(items.begin(), items.end(),
                     [](const MediaItem *a, const MediaItem *b) { return a->mDateTakenInMs < b->mDateTakenInMs; });

    mClustering.clear();
    if (!items.empty()) {
        int64_t range = items.front()->mDateTakenInMs - items.back()->mDateTakenInMs;
        if (range < 0) {
            range = -range;
        }
        mClustering.setTimeRange(range, (int)items.size());
    }
    for (MediaItem *item : items) {
        mClustering.addItemForClustering(item);
    }
    mClustering.compute(nullptr, true);
    mClustering.generateCaptions();

    mInClusteringMode = true;
    updateListener(true);
}

bool MediaFeed::restorePreviousClusteringState() {
    if (!mInClusteringMode) {
        return false;
    }
    mInClusteringMode = false;
    if (mListener) {
        mListener->onFeedAboutToChange(this);
    }
    updateListener(true);
    return true;
}

void MediaFeed::copySlotStateFrom(const MediaFeed &another) {
    mExpandedMediaSetIndex = another.mExpandedMediaSetIndex;
    mInClusteringMode = another.mInClusteringMode;
}

void MediaFeed::performOperation(int operation, std::vector<MediaBucket> *mediaBuckets, const void *data) {
    if (mediaBuckets == nullptr) {
        return;
    }

    // Collect first. Deleting walks the same sets these buckets point into, so
    // mutating while iterating them would be asking for trouble.
    std::vector<MediaItem *> items;
    for (const MediaBucket &bucket : *mediaBuckets) {
        if (bucket.hasItems) {
            items.insert(items.end(), bucket.mediaItems.begin(), bucket.mediaItems.end());
        } else if (bucket.mediaSet != nullptr) {
            const std::vector<MediaItem *> &setItems = bucket.mediaSet->getItems();
            items.insert(items.end(), setItems.begin(), setItems.end());
        }
    }
    if (items.empty()) {
        return;
    }

    // Perform storage writes on the worker. pumpListener applies successful deletions
    // between frames because the renderer holds raw MediaItem pointers.
    if (operation == OPERATION_DELETE) {
        postJob([this, operation, items]() {
            std::vector<MediaItem *> deleted;
            for (MediaItem *item : items) {
                if (mShuttingDown.load()) {
                    return;
                }
                if (performOperationOnItem(operation, item, nullptr)) {
                    deleted.push_back(item);
                }
            }
            if (deleted.empty()) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mDeletedMutex);
                mDeletedItems.insert(mDeletedItems.end(), deleted.begin(), deleted.end());
            }
            updateListener(true);
        });
        return;
    }

    if (operation == OPERATION_ROTATE) {
        float degrees = (data != nullptr) ? *(const float *)data : 0.0f;
        // Update visual rotation immediately on the render thread.
        for (MediaItem *item : items) {
            if (item != nullptr) {
                item->mRotation = Shared::normalizePositive(item->mRotation + degrees);
            }
        }
        // Whether the turn outlives the session is up to the source, and that
        // is the part that touches storage, so it waits its turn on the worker.
        std::vector<std::pair<MediaItem *, float>> rotations;
        rotations.reserve(items.size());
        for (MediaItem *item : items) {
            if (item != nullptr) {
                rotations.emplace_back(item, item->mRotation);
            }
        }
        postJob([this, operation, rotations]() {
            for (const std::pair<MediaItem *, float> &entry : rotations) {
                if (mShuttingDown.load()) {
                    return;
                }
                float rotation = entry.second;
                performOperationOnItem(operation, entry.first, &rotation);
            }
        });
        return;
    }
}

bool MediaFeed::selectionSupports(int operation, const std::vector<MediaBucket> *mediaBuckets) const {
    // Nothing picked yet, so answer for the source behind the feed. Otherwise
    // the bar would offer a button for the moment between entering selection
    // mode and the first item being chosen, then take it away again.
    if (mediaBuckets == nullptr || mediaBuckets->empty()) {
        return mDataSource != nullptr && mDataSource->supportsOperation(operation);
    }
    bool sawItem = false;
    for (const MediaBucket &bucket : *mediaBuckets) {
        std::vector<MediaItem *> items;
        if (bucket.hasItems) {
            items = bucket.mediaItems;
        } else if (bucket.mediaSet != nullptr) {
            items = bucket.mediaSet->getItems();
        }
        for (const MediaItem *item : items) {
            if (item == nullptr) {
                continue;
            }
            sawItem = true;
            const MediaSet *set = item->mParentMediaSet;
            const DataSource *source = (set != nullptr && set->mDataSource != nullptr) ? set->mDataSource
                                                                                      : mDataSource;
            if (source == nullptr || !source->supportsOperation(operation)) {
                return false;
            }
        }
    }
    if (!sawItem) {
        return mDataSource != nullptr && mDataSource->supportsOperation(operation);
    }
    return true;
}

bool MediaFeed::performOperationOnItem(int operation, MediaItem *item, const void *data) {
    MediaSet *set = (item != nullptr) ? item->mParentMediaSet : nullptr;
    DataSource *source = (set != nullptr && set->mDataSource != nullptr) ? set->mDataSource : mDataSource;
    if (source == nullptr) {
        return false;
    }
    // Only what the source confirmed counts, so the wall never shows a state
    // the storage behind it does not agree with.
    return source->performOperation(operation, item, data);
}

void MediaFeed::removeItem(MediaItem *item) {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    for (std::unique_ptr<MediaSet> &set : mMediaSets) {
        if (set->removeItem(item)) {
            set->updateNumExpectedItems();
            set->generateTitle(true);
            break;
        }
    }
    // The clusters reference the same items, so they have to let go too or the
    // timeline would draw a deleted photo.
    for (std::unique_ptr<MediaSet> &cluster : mClustering.getClustersForDisplay()) {
        cluster->removeItem(item);
    }
}

void MediaFeed::setFilter(void *filter) {
    (void)filter;
}

void MediaFeed::removeFilter() {}
