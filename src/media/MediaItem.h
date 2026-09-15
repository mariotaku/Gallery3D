// Port of com.cooliris.media.MediaItem.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

class MediaSet;

class MediaItem {
  public:
    static const int MEDIA_TYPE_IMAGE = 0;
    static const int MEDIA_TYPE_VIDEO = 1;

    // Known capture-date precision. Year-only catalogue dates use January 1 internally
    // but display only the year.
    enum DatePrecision {
        PRECISION_DAY = 0,  // the default: an instant, as a camera records it
        PRECISION_MONTH = 1,
        PRECISION_YEAR = 2,
    };

    // Taken dates are valid when nonzero, including pre-1970 years; zero means unknown.
    // Filesystem-added dates retain their sanity bounds.
    static const int64_t MIN_VALID_DATE_IN_SEC = 157680000LL;
    static const int64_t MAX_VALID_DATE_IN_SEC = 2049840000LL;

    int64_t mId = -1;
    std::string mCaption;
    std::string mFilePath;
    // Content addresses; local files use the same path at three decode sizes.
    std::string mContentUri;
    std::string mThumbnailUri;
    std::string mScreennailUri;
    std::string mMimeType;

    double mLatitude = 0.0;
    double mLongitude = 0.0;

    int64_t mDateTakenInMs = 0;
    // Sources may reduce precision for partially known dates.
    int mDatePrecision = PRECISION_DAY;
    int64_t mDateModifiedInSec = 0;
    int64_t mDateAddedInSec = 0;

    float mRotation = 0.0f;

    // Original dimensions for tiling, or zero if unknown. Screennail dimensions
    // are downscaled and cannot substitute for these.
    int mFullWidth = 0;
    int mFullHeight = 0;

    bool hasFullSize() const {
        return mFullWidth > 0 && mFullHeight > 0;
    }

    MediaSet *mParentMediaSet = nullptr;

    // What a source finds out only when the photo is about to be shown, such
    // as a folder tree's EXIF. A loader thread fills mLateDetails once and then
    // sets mLateDetailsPending. The render thread applies them with
    // takeLateDetails.
    struct LateDetails {
        float rotation = 0.0f;
        int64_t dateTakenMs = 0;
        int width = 0;
        int height = 0;
    };
    LateDetails mLateDetails;
    std::atomic<bool> mLateDetailsPending{false};

    // Applies late details, on the render thread. True when there were some.
    bool takeLateDetails() {
        if (!mLateDetailsPending.exchange(false)) {
            return false;
        }
        mRotation = mLateDetails.rotation;
        if (mLateDetails.dateTakenMs != 0) {
            mDateTakenInMs = mLateDetails.dateTakenMs;
        }
        if (mLateDetails.width > 0 && mLateDetails.height > 0) {
            mFullWidth = mLateDetails.width;
            mFullHeight = mLateDetails.height;
        }
        return true;
    }

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
