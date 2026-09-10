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

void ConcatenatedDataSource::shutdown() {
    if (mFirst != nullptr) {
        mFirst->shutdown();
    }
    if (mSecond != nullptr) {
        mSecond->shutdown();
    }
}
