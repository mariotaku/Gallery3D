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

    static const int64_t MIN_VALID_DATE_IN_MS = 157680000000LL;
    static const int64_t MAX_VALID_DATE_IN_MS = 2049840000000LL;
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
        return mDateTakenInMs > MIN_VALID_DATE_IN_MS && mDateTakenInMs < MAX_VALID_DATE_IN_MS;
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
