#include "media/ConcatenatedDataSource.h"

#include "media/MediaFeed.h"
#include "media/MediaSet.h"

void ConcatenatedDataSource::loadMediaSets(MediaFeed *feed) {
    // Enumerate sources in order; addMediaSet records each set's owner.
    if (mFirst != nullptr) {
        mFirst->loadMediaSets(feed);
    }
    if (mSecond != nullptr) {
        mSecond->loadMediaSets(feed);
    }
}

void ConcatenatedDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    // Route to the set's owner.
    DataSource *owner = (parentSet != nullptr) ? parentSet->mDataSource : nullptr;
    if (owner != nullptr && owner != this) {
        owner->loadItemsForSet(feed, parentSet);
        return;
    }
    // Nobody else will load this set, so it is finished now. Otherwise the
    // feed would keep it in flight and never ask for its items again.
    if (feed != nullptr) {
        feed->finishLoadingItemsForSet(parentSet);
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
    // Without an item, report whether either source supports the operation.
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

void ConcatenatedDataSource::requestItemBytes(MediaItem *item, BytesCallback done) {
    MediaSet *set = (item != nullptr) ? item->mParentMediaSet : nullptr;
    DataSource *owner = (set != nullptr) ? set->mDataSource : nullptr;
    if (owner == nullptr || owner == this) {
        if (done) {
            done(false, std::vector<uint8_t>());
        }
        return;
    }
    owner->requestItemBytes(item, std::move(done));
}

bool ConcatenatedDataSource::readThumbnail(MediaItem *item, int maxEdge, Bitmap *bitmap) {
    MediaSet *set = (item != nullptr) ? item->mParentMediaSet : nullptr;
    DataSource *owner = (set != nullptr) ? set->mDataSource : nullptr;
    if (owner == nullptr || owner == this) {
        return false;
    }
    return owner->readThumbnail(item, maxEdge, bitmap);
}

void ConcatenatedDataSource::shutdown() {
    if (mFirst != nullptr) {
        mFirst->shutdown();
    }
    if (mSecond != nullptr) {
        mSecond->shutdown();
    }
}
