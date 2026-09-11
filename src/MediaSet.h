// Port of com.cooliris.media.MediaSet. Owns items through unique_ptr; clusters reference them.
#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "MediaItem.h"
#include "Shared.h"

class MediaSet {
  public:
    static const int TYPE_SMART = 0;
    static const int TYPE_FOLDER = 1;
    static const int TYPE_USERDEFINED = 2;

    int64_t mId = Shared::INVALID;
    std::string mName;
    int mType = TYPE_FOLDER;

    // An inverted min/max pair means unset; valid pre-1970 bounds may both be negative.
    int64_t mMinTimestamp = std::numeric_limits<int64_t>::max();
    int64_t mMaxTimestamp = std::numeric_limits<int64_t>::lowest();
    int mDatePrecision = MediaItem::PRECISION_DAY;
    int64_t mMinAddedTimestamp = std::numeric_limits<int64_t>::max();
    int64_t mMaxAddedTimestamp = std::numeric_limits<int64_t>::lowest();

    double mMinLatLatitude = 91.0;
    double mMinLatLongitude = 0.0;
    double mMaxLatLatitude = -91.0;
    double mMaxLatLongitude = 0.0;
    double mMinLonLatitude = 0.0;
    double mMinLonLongitude = 181.0;
    double mMaxLonLatitude = 0.0;
    double mMaxLonLongitude = -181.0;

    std::string mReverseGeocodedLocation;
    bool mLatLongDetermined = false;
    bool mReverseGeocodedLocationComputed = false;
    bool mReverseGeocodedLocationRequestMade = false;

    std::string mTitleString;
    std::string mTruncTitleString;
    std::string mNoCountTitleString;

    int64_t mPicasaAlbumId = Shared::INVALID;
    bool mIsLocal = true;

    // Whoever created this set. The feed routes anything set specific back
    // here rather than to its own source, which is what lets more than one
    // source share a feed.
    class DataSource *mDataSource = nullptr;

    std::vector<MediaItem *> &getItems() {
        return mItems;
    }

    const std::vector<MediaItem *> &getItems() const {
        return mItems;
    }

    int getNumItems() const {
        return (int)mItems.size();
    }

    int getNumExpectedItems() const {
        return mNumExpectedItems;
    }

    void setNumExpectedItems(int numExpectedItems) {
        mNumExpectedItems = numExpectedItems;
        mNumExpectedItemsCountAccurate = true;
    }

    void updateNumExpectedItems() {
        mNumExpectedItems = (int)mItems.size();
        mNumExpectedItemsCountAccurate = true;
    }

    bool isPicassaAlbum() const {
        return mPicasaAlbumId != Shared::INVALID;
    }

    bool areTimestampsAvailable() const {
        return mMinTimestamp <= mMaxTimestamp;
    }

    // The coarsest precision of anything in here, since a caption can only be
    // as precise as its vaguest member.
    int datePrecision() const {
        return mDatePrecision;
    }

    bool areAddedTimestampsAvailable() const {
        return mMinAddedTimestamp <= mMaxAddedTimestamp;
    }

    // Takes ownership and folds the item's time and location into the set bounds.
    void addItem(std::unique_ptr<MediaItem> item);

    // Sort oldest first after each batch for timeline and clustering.
    // Undated items go last in stable arrival order.
    void sortItemsByDate();

    // References an item owned by another set. Used by the clustering pass.
    void addItemRef(MediaItem *item);

    void generateTitle(bool truncateTitle);

    // Drops the item and, if this set owns it, frees it. Returns false when the
    // item is not in this set.
    bool removeItem(MediaItem *item);

  private:
    std::vector<MediaItem *> mItems;
    std::vector<std::unique_ptr<MediaItem>> mOwnedItems;
    int mNumExpectedItems = 0;
    bool mNumExpectedItemsCountAccurate = false;
};
