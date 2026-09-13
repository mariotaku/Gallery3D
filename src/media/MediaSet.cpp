#include "media/MediaSet.h"

#include <algorithm>

void MediaSet::addItemRef(MediaItem *item) {
    if (!item) {
        return;
    }
    mItems.push_back(item);

    if (item->isDateTakenValid()) {
        // The coarsest wins: a caption can only be as precise as its vaguest
        // member.
        if (item->mDatePrecision > mDatePrecision) {
            mDatePrecision = item->mDatePrecision;
        }
        int64_t dateTaken = item->mDateTakenInMs;
        if (dateTaken < mMinTimestamp) {
            mMinTimestamp = dateTaken;
        }
        if (dateTaken > mMaxTimestamp) {
            mMaxTimestamp = dateTaken;
        }
    } else if (item->isDateAddedValid()) {
        int64_t dateAdded = item->mDateAddedInSec * 1000;
        if (dateAdded < mMinAddedTimestamp) {
            mMinAddedTimestamp = dateAdded;
        }
        if (dateAdded > mMaxAddedTimestamp) {
            mMaxAddedTimestamp = dateAdded;
        }
    }

    if (!item->isLatLongValid()) {
        return;
    }
    double itemLatitude = item->mLatitude;
    double itemLongitude = item->mLongitude;
    if (mMinLatLatitude > itemLatitude) {
        mMinLatLatitude = itemLatitude;
        mMinLatLongitude = itemLongitude;
        mLatLongDetermined = true;
    }
    if (mMaxLatLatitude < itemLatitude) {
        mMaxLatLatitude = itemLatitude;
        mMaxLatLongitude = itemLongitude;
        mLatLongDetermined = true;
    }
    if (mMinLonLongitude > itemLongitude) {
        mMinLonLatitude = itemLatitude;
        mMinLonLongitude = itemLongitude;
        mLatLongDetermined = true;
    }
    if (mMaxLonLongitude < itemLongitude) {
        mMaxLonLatitude = itemLatitude;
        mMaxLonLongitude = itemLongitude;
        mLatLongDetermined = true;
    }
}

void MediaSet::sortItemsByDate() {
    std::stable_sort(mItems.begin(), mItems.end(), [](const MediaItem *a, const MediaItem *b) {
        const bool aDated = a->mDateTakenInMs != 0;
        const bool bDated = b->mDateTakenInMs != 0;
        if (aDated != bDated) {
            return aDated;
        }
        if (!aDated) {
            // Both undated: stable_sort keeps them in the order they arrived.
            return false;
        }
        return a->mDateTakenInMs < b->mDateTakenInMs;
    });
}

void MediaSet::addItem(std::unique_ptr<MediaItem> item) {
    if (!item) {
        return;
    }
    MediaItem *raw = item.get();
    raw->mParentMediaSet = this;
    mOwnedItems.push_back(std::move(item));
    addItemRef(raw);
}

void MediaSet::generateTitle(bool truncateTitle) {
    std::string size =
        mNumExpectedItemsCountAccurate ? ("  (" + std::to_string(mNumExpectedItems) + ")") : std::string();
    mTitleString = mName + size;
    if (truncateTitle) {
        size_t length = mName.length();
        mTruncTitleString =
            (length > 16) ? (mName.substr(0, 12) + "..." + mName.substr(length - 4, 4) + size) : (mName + size);
        mNoCountTitleString = mName;
    } else {
        mTruncTitleString = mTitleString;
        mNoCountTitleString = mName;
    }
}

bool MediaSet::removeItem(MediaItem *item) {
    auto found = std::find(mItems.begin(), mItems.end(), item);
    if (found == mItems.end()) {
        return false;
    }
    mItems.erase(found);
    // A cluster only references items; the album that owns them frees them.
    for (size_t i = 0; i < mOwnedItems.size(); ++i) {
        if (mOwnedItems[i].get() == item) {
            mOwnedItems.erase(mOwnedItems.begin() + (long)i);
            break;
        }
    }
    return true;
}
