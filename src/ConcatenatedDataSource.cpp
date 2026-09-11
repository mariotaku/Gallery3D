#include "ConcatenatedDataSource.h"

#include "MediaSet.h"

void ConcatenatedDataSource::loadMediaSets(MediaFeed *feed) {
    // In order, so the first source's albums come first on the wall. Each one
    // passes itself to addMediaSet, which is what makes the routing below work.
    if (mFirst != nullptr) {
        mFirst->loadMediaSets(feed);
    }
    if (mSecond != nullptr) {
        mSecond->loadMediaSets(feed);
    }
}

void ConcatenatedDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    if (parentSet == nullptr) {
        return;
    }
    // Straight to the owner. Asking both would let the wrong source fill a set
    // that is not its own.
    DataSource *owner = parentSet->mDataSource;
    if (owner != nullptr && owner != this) {
        owner->loadItemsForSet(feed, parentSet);
    }
}

bool ConcatenatedDataSource::performOperation(int operation, MediaItem *item, const void *data) {
    MediaSet *set = (item != nullptr) ? item->mParentMediaSet : nullptr;
    DataSource *owner = (set != nullptr) ? set->mDataSource : nullptr;
    if (owner == nullptr || owner == this) {
        return false;
    }
    return owner->performOperation(operation, item, data);
}

bool ConcatenatedDataSource::supportsOperation(int operation) const {
    // Asked without an item in hand, so the answer is whether anything behind
    // here could do it. Once there is an item, the routing above picks the one
    // source that owns it and its answer is the one that counts.
    return (mFirst != nullptr && mFirst->supportsOperation(operation)) ||
           (mSecond != nullptr && mSecond->supportsOperation(operation));
}

bool ConcatenatedDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    MediaSet *set = (item != nullptr) ? item->mParentMediaSet : nullptr;
    DataSource *owner = (set != nullptr) ? set->mDataSource : nullptr;
    if (owner == nullptr || owner == this) {
        return false;
    }
    return owner->readItemBytes(item, bytes);
}

void ConcatenatedDataSource::shutdown() {
    if (mFirst != nullptr) {
        mFirst->shutdown();
    }
    if (mSecond != nullptr) {
        mSecond->shutdown();
    }
}
