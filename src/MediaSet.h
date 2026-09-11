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

    // Unset is the pair being the wrong way round, not zero. A set whose
    // photographs all predate 1970 has a negative maximum, and testing that
    // against zero called every one of them undated.
    int64_t mMinTimestamp = std::numeric_limits<int64_t>::max();
    int64_t mMaxTimestamp = std::numeric_limits<int64_t>::lowest();
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

    bool areAddedTimestampsAvailable() const {
        return mMinAddedTimestamp <= mMaxAddedTimestamp;
    }

    // Takes ownership and folds the item's time and location into the set bounds.
    void addItem(std::unique_ptr<MediaItem> item);

    // Oldest first, which is the order the original's albums came in: its
    // queries ended in DATE_TAKEN ASC, so the set was already sorted by the
    // time anything drew it, and the time bar and the clusterer both read the
    // sequence as a timeline.
    //
    // A source here delivers whatever order it has - a directory listing, an
    // api's idea of relevance - so the sort has to happen after. Call it once a
    // batch has been added rather than per item.
    //
    // Items with no date keep their arrival order and go last. Sorting them to
    // the front by their zero timestamp would put everything undated before
    // every dated thing, which for a museum's catalogue is most of the wall.
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
