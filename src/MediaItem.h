// Port of com.cooliris.media.MediaItem, trimmed to the fields a local
// filesystem source can fill in.
#pragma once

#include <cstdint>
#include <string>

class MediaSet;

class MediaItem {
  public:
    static const int MEDIA_TYPE_IMAGE = 0;
    static const int MEDIA_TYPE_VIDEO = 1;

    // The original bounded a valid capture date to between the end of 1974 and
    // 2034, on the reasoning that nothing it would ever show predates a digital
    // camera. That is true of a phone's camera roll and false of anything else:
    // a museum's catalogue runs from antiquity to last year, and under those
    // bounds every one of its artworks counted as having no date at all - which
    // left the time bar with no range to scrub and the clusterer with nothing
    // to group.
    //
    // So a taken date is now valid whenever it is set. Zero still means unknown,
    // which is what an item starts as and what a source leaves it as when it has
    // no date to give.
    //
    // The added date keeps its bounds. It comes from the filesystem rather than
    // from the picture, so it really is a recent timestamp or a broken one, and
    // the range is a sanity check rather than an assumption about the subject.
    static const int64_t MIN_VALID_DATE_IN_SEC = 157680000LL;
    static const int64_t MAX_VALID_DATE_IN_SEC = 2049840000LL;

    int64_t mId = -1;
    std::string mCaption;
    std::string mFilePath;
    // The original addressed images by content Uri. Local files use the path
    // for all three, at different decode sizes.
    std::string mContentUri;
    std::string mThumbnailUri;
    std::string mScreennailUri;
    std::string mMimeType;

    double mLatitude = 0.0;
    double mLongitude = 0.0;

    int64_t mDateTakenInMs = 0;
    int64_t mDateModifiedInSec = 0;
    int64_t mDateAddedInSec = 0;

    float mRotation = 0.0f;

    MediaSet *mParentMediaSet = nullptr;

    bool isDateTakenValid() const {
        return mDateTakenInMs != 0;
    }

    bool isDateAddedValid() const {
        return mDateAddedInSec > MIN_VALID_DATE_IN_SEC && mDateAddedInSec < MAX_VALID_DATE_IN_SEC;
    }

    bool isLatLongValid() const {
        return mLatitude != 0.0 || mLongitude != 0.0;
    }

    int getMediaType() const {
        if (mMediaType == -1) {
            mMediaType = (mMimeType.rfind("video/", 0) == 0) ? MEDIA_TYPE_VIDEO : MEDIA_TYPE_IMAGE;
        }
        return mMediaType;
    }

    void setMediaType(int mediaType) {
        mMediaType = mediaType;
    }

  private:
    mutable int mMediaType = -1;
};
