#include "MediaFeed.h"

#include <algorithm>
#include <cstdlib>

#include "LocalDataSource.h"

MediaFeed::MediaFeed(DataSource *dataSource, Listener *listener) : mDataSource(dataSource), mListener(listener) {}

MediaSet *MediaFeed::addMediaSet(int64_t setId) {
    std::lock_guard<std::mutex> lock(mSetsMutex);
    for (size_t i = 0; i < mMediaSets.size(); ++i) {
        if (mMediaSets[i]->mId == setId) {
            // The set already exists but may be out of date. Drop it so the
            // fresh one below replaces it rather than doubling up.
            mMediaSets.erase(mMediaSets.begin() + (long)i);
            break;
        }
    }
    mMediaSets.push_back(std::make_unique<MediaSet>());
    mMediaSets.back()->mId = setId;
    return mMediaSets.back().get();
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
    return (int)mClusters.size();
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
    if (slotIndex < (int)mClusters.size()) {
        return mClusters[(size_t)slotIndex].get();
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
    mClusters.clear();

    // The original ran a multi pass clusterer over time and location. This is
    // the simple version: start a new cluster wherever the gap between
    // consecutive shots is more than an hour.
    const int64_t kSplitGapMs = 60LL * 60LL * 1000LL;
    std::vector<MediaItem *> items = setToUse->getItems();
    std::sort(items.begin(), items.end(),
              [](const MediaItem *a, const MediaItem *b) { return a->mDateTakenInMs > b->mDateTakenInMs; });

    MediaSet *current = nullptr;
    int64_t previousTime = 0;
    for (MediaItem *item : items) {
        int64_t time = item->mDateTakenInMs;
        bool startNew = (current == nullptr) || (previousTime != 0 && std::llabs(previousTime - time) > kSplitGapMs);
        if (startNew) {
            mClusters.push_back(std::make_unique<MediaSet>());
            current = mClusters.back().get();
            current->mId = (int64_t)mClusters.size();
            current->mType = MediaSet::TYPE_SMART;
        }
        current->addItemRef(item);
        previousTime = time;
    }
    for (std::unique_ptr<MediaSet> &cluster : mClusters) {
        cluster->updateNumExpectedItems();
        cluster->mName = setToUse->mName;
        cluster->generateTitle(true);
    }

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

void MediaFeed::performOperation(int operation, void *mediaBuckets, void *data) {
    (void)operation;
    (void)mediaBuckets;
    (void)data;
}

void MediaFeed::setFilter(void *filter) {
    (void)filter;
}

void MediaFeed::removeFilter() {}
