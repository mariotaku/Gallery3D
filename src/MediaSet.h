// Port of com.cooliris.media.MediaSet.
//
// The original owned MediaItems by value in an ArrayList that a background
// content observer mutated. The port owns them through unique_ptr and builds a
// set once, so the incremental add/remove/lookup machinery is gone.
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

    int64_t mMinTimestamp = std::numeric_limits<int64_t>::max();
    int64_t mMaxTimestamp = 0;
    int64_t mMinAddedTimestamp = std::numeric_limits<int64_t>::max();
    int64_t mMaxAddedTimestamp = 0;

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
        return mMinTimestamp < std::numeric_limits<int64_t>::max() && mMaxTimestamp > 0;
    }

    // Takes ownership and folds the item's time and location into the set bounds.
    void addItem(std::unique_ptr<MediaItem> item);

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
