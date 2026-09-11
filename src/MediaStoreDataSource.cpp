#include "MediaStoreDataSource.h"

#if defined(__ANDROID__)

#include <memory>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "AndroidBridge.h"
#include "JsonValue.h"
#include "MediaFeed.h"
#include "MediaItem.h"
#include "MediaSet.h"

namespace {

// MediaStore's bucket id is a string. The wall keys its sets on a number, so
// the string is folded into one and the pair is remembered to query with later.
int64_t hashBucketId(const std::string &bucketId) {
    int64_t hash = 1469598103934665603LL;
    for (unsigned char character : bucketId) {
        hash ^= (int64_t)character;
        hash *= 1099511628211LL;
    }
    return hash;
}

// MediaStore reports orientation in degrees already, unlike EXIF's tag numbers.
float rotationFor(int orientation) {
    switch (((orientation % 360) + 360) % 360) {
    case 90:
        return 90.0f;
    case 180:
        return 180.0f;
    case 270:
        return 270.0f;
    default:
        return 0.0f;
    }
}

nlohmann::json parse(const std::string &text, const char *what) {
    if (text.empty()) {
        return nlohmann::json::array();
    }
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        SDL_Log("Could not read the %s the media store returned", what);
        return nlohmann::json::array();
    }
    return parsed;
}

}  // namespace

void MediaStoreDataSource::loadMediaSets(MediaFeed *feed) {
    if (feed == nullptr) {
        return;
    }
    // The dialog needs an answer before any query returns rows. This runs on a
    // loader thread, so waiting here does not hold up the wall.
    if (!AndroidBridge::requestMediaPermission()) {
        feed->finishLoadingMediaSets();
        return;
    }

    const nlohmann::json buckets = parse(AndroidBridge::queryBuckets(), "folder list");
    SDL_Log("The media store has %d folders of photos", (int)buckets.size());

    for (const nlohmann::json &bucket : buckets) {
        const std::string bucketId = stringOr(bucket, "id", "");
        if (bucketId.empty()) {
            continue;
        }
        const int64_t setId = hashBucketId(bucketId);
        {
            std::lock_guard<std::mutex> lock(mBucketsMutex);
            mBuckets[setId] = bucketId;
        }

        MediaSet *set = feed->addMediaSet(setId, this);
        set->mName = stringOr(bucket, "name", bucketId.c_str());
        set->mType = MediaSet::TYPE_FOLDER;
        set->mIsLocal = true;
        set->setNumExpectedItems((int)intOr(bucket, "count", 0));

        // The wall draws a stack from the photos in it, so the items come with
        // the folder rather than waiting to be paged in.
        loadBucketItems(feed, set, bucketId);
    }
    feed->finishLoadingMediaSets();
}

void MediaStoreDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    // loadMediaSets already filled every set, the same way the desktop source
    // does after its scan.
    if (feed != nullptr) {
        feed->finishLoadingItemsForSet(parentSet);
    }
}

void MediaStoreDataSource::loadBucketItems(MediaFeed *feed, MediaSet *parentSet,
                                           const std::string &bucketId) {
    if (feed == nullptr || parentSet == nullptr || bucketId.empty()) {
        return;
    }

    const nlohmann::json photos = parse(AndroidBridge::queryBucket(bucketId), "photo list");
    for (const nlohmann::json &photo : photos) {
        const int64_t id = intOr(photo, "id", -1);
        if (id < 0) {
            continue;
        }
        auto item = std::make_unique<MediaItem>();
        item->mId = id;
        // No file path: scoped storage means the app may not open one. Every
        // read goes back through the content resolver by id.
        item->mMimeType = stringOr(photo, "mime", "image/jpeg");
        item->mCaption = stringOr(photo, "name", "");
        item->mRotation = rotationFor((int)intOr(photo, "orientation", 0));
        item->mFullWidth = (int)intOr(photo, "width", 0);
        item->mFullHeight = (int)intOr(photo, "height", 0);
        item->mDateTakenInMs = intOr(photo, "dateTaken", 0);
        item->mDateModifiedInSec = intOr(photo, "dateModified", 0);
        item->mDateAddedInSec = intOr(photo, "dateAdded", 0);
        // MediaStore leaves dateTaken at zero for a photo with no exif date.
        if (item->mDateTakenInMs == 0 && item->mDateModifiedInSec != 0) {
            item->mDateTakenInMs = item->mDateModifiedInSec * 1000LL;
        }
        parentSet->addItem(std::move(item));
    }

    parentSet->sortItemsByDate();
    parentSet->updateNumExpectedItems();
    parentSet->generateTitle(true);
    feed->updateListener(true);
}

bool MediaStoreDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    if (item == nullptr || item->mId < 0) {
        return false;
    }
    return AndroidBridge::readImage(item->mId, bytes);
}

std::string MediaStoreDataSource::bucketIdForSet(int64_t setId) const {
    std::lock_guard<std::mutex> lock(mBucketsMutex);
    auto found = mBuckets.find(setId);
    return (found != mBuckets.end()) ? found->second : std::string();
}

#endif  // __ANDROID__
