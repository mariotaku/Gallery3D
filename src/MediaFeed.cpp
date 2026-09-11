#include "MediaFeed.h"

#include <algorithm>
#include <cstdlib>

#include "FileOperations.h"
#include "LocalDataSource.h"

MediaFeed::MediaFeed(DataSource *dataSource, Listener *listener) : mDataSource(dataSource), mListener(listener) {}

MediaSet *MediaFeed::addMediaSet(int64_t setId, DataSource *source) {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    for (size_t i = 0; i < mMediaSets.size(); ++i) {
        if (mMediaSets[i]->mId == setId) {
            // Gingerbread fix: the set already exists but may be out of date.
            // Drop it so the fresh one below replaces it rather than doubling
            // up, which is what a rescan used to do.
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
    if (set == nullptr || set->getNumItems() > 0) {
        return;
    }
    DataSource *source = (set->mDataSource != nullptr) ? set->mDataSource : mDataSource;
    if (source != nullptr) {
        source->loadItemsForSet(this, set);
    }
}

MediaFeed::~MediaFeed() {
    shutdown();
}

void MediaFeed::start() {
    if (mLoaderThread.joinable()) {
        return;
    }
    mLoading.store(true);
    mLoaderThread = std::thread([this]() { loaderThread(); });
}

void MediaFeed::shutdown() {
    mShuttingDown.store(true);
    if (mLoaderThread.joinable()) {
        mLoaderThread.join();
    }
    if (mDataSource) {
        mDataSource->shutdown();
    }
}

void MediaFeed::loaderThread() {
    if (mDataSource) {
        mDataSource->loadMediaSets(this);
    }
    mLoading.store(false);
    updateListener(true);
}

void MediaFeed::pumpListener() {
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
    // The original used this to prioritise background loading of set contents.
    // The local source loads everything up front, so nothing to do.
}

void MediaFeed::expandMediaSet(int mediaSetIndex) {
    if (mListener) {
        mListener->onFeedAboutToChange(this);
    }
    {
        // A source that fills its sets lazily needs the items before the slot
        // model is rebuilt around them.
        std::lock_guard<std::mutex> lock(mSetsMutex);
        if (mediaSetIndex >= 0 && mediaSetIndex < (int)mMediaSets.size()) {
            MediaSet *set = mMediaSets[(size_t)mediaSetIndex].get();
            if (set->getNumItems() == 0 && set->mDataSource != nullptr) {
                set->mDataSource->loadItemsForSet(this, set);
            }
        }
    }
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

    // Newest first, the order the original's feed delivered items in. The
    // clusterer walks the sequence and only ever compares neighbours, so the
    // order is what makes a run of shots a run.
    std::vector<MediaItem *> items = setToUse->getItems();
    std::sort(items.begin(), items.end(),
              [](const MediaItem *a, const MediaItem *b) { return a->mDateTakenInMs > b->mDateTakenInMs; });

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

    // Whoever owns the item carries the operation out. The feed only updates
    // itself for the ones that reported success, so a source that cannot do
    // something leaves the wall matching its storage rather than diverging
    // from it.
    auto sourceFor = [this](MediaItem *item) -> DataSource * {
        MediaSet *set = (item != nullptr) ? item->mParentMediaSet : nullptr;
        if (set != nullptr && set->mDataSource != nullptr) {
            return set->mDataSource;
        }
        return mDataSource;
    };

    if (operation == OPERATION_DELETE) {
        int deleted = 0;
        for (MediaItem *item : items) {
            DataSource *source = sourceFor(item);
            if (source != nullptr && source->performOperation(operation, item, data)) {
                removeItem(item);
                ++deleted;
            }
        }
        if (deleted > 0) {
            if (mListener) {
                mListener->onFeedAboutToChange(this);
            }
            updateListener(true);
        }
        return;
    }

    if (operation == OPERATION_ROTATE) {
        float degrees = (data != nullptr) ? *(const float *)data : 0.0f;
        for (MediaItem *item : items) {
            if (item == nullptr) {
                continue;
            }
            // The wall turns either way. Whether the turn outlives the session
            // is up to the source, which is why its answer is not checked here.
            float rotation = Shared::normalizePositive(item->mRotation + degrees);
            item->mRotation = rotation;
            DataSource *source = sourceFor(item);
            if (source != nullptr) {
                source->performOperation(operation, item, &rotation);
            }
        }
        return;
    }
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
