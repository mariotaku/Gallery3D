#include "media/MediaStoreDataSource.h"

#include <algorithm>
#include <memory>
#include <unordered_set>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "core/JsonValue.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"

namespace {

// What MediaStore.Images.Media.EXTERNAL_CONTENT_URI spells out to. An item's
// own uri is this plus its id.
const char *const kImagesUri = "content://media/external/images/media/";

// MediaStore's bucket id is a string. The wall keys its sets on a number, so
// the string is folded into one and the pair is remembered to query with later.
// Kept non-negative, as LocalDataSource keeps its path hashes.
int64_t hashBucketId(const std::string &bucketId) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char character : bucketId) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return (int64_t)(hash & 0x7FFFFFFFFFFFFFFFULL);
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
    if (!mClient.requestPermission()) {
        feed->finishLoadingMediaSets();
        return;
    }

    const nlohmann::json buckets = parse(mClient.queryBuckets(), "folder list");
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

        auto set = std::make_unique<MediaSet>();
        set->mId = setId;
        set->mDataSource = this;
        set->mName = stringOr(bucket, "name", bucketId.c_str());
        set->mType = MediaSet::TYPE_FOLDER;
        set->mIsLocal = true;
        // The bridge compares bucket ids, which is what the media store keys a
        // folder by.
        set->mIsCameraRoll = bucket.value("camera", false);
        set->setNumExpectedItems((int)intOr(bucket, "count", 0));

        // The wall draws a stack from the photos in it, so the items come with
        // the folder rather than waiting to be paged in. The set goes to the
        // feed whole once they are in.
        loadBucketItems(*set, bucketId);
        feed->addMediaSet(std::move(set));
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

void MediaStoreDataSource::loadBucketItems(MediaSet &set, const std::string &bucketId) {
    if (bucketId.empty()) {
        return;
    }

    const nlohmann::json photos = parse(mClient.queryBucket(bucketId), "photo list");
    // A bucket is one folder, so names alone find a RAW and the JPEG or HEIF
    // beside it, which the desktop shows as one photo.
    std::vector<std::string> names;
    names.reserve(photos.size());
    for (const nlohmann::json &photo : photos) {
        names.push_back(stringOr(photo, "name", ""));
    }
    std::unordered_set<std::string> kept;
    for (std::string &name : LocalDataSource::withoutRawDuplicates(std::move(names))) {
        kept.insert(std::move(name));
    }

    for (const nlohmann::json &photo : photos) {
        const int64_t id = intOr(photo, "id", -1);
        if (id < 0 || kept.count(stringOr(photo, "name", "")) == 0) {
            continue;
        }
        auto item = std::make_unique<MediaItem>();
        item->mId = id;
        // No file path: scoped storage means the app may not open one. Every
        // read goes back through the content resolver by this uri. The three
        // sizes name the same photo, as they do for a local file, and the
        // decoders reach it through readItemBytes rather than by opening the
        // string. The screennail one has to be set: an empty one is how a
        // display item spells "this photo has no fullscreen image", which
        // leaves it drawing an enlarged thumbnail and never tiling.
        item->mContentUri = kImagesUri + std::to_string(id);
        item->mThumbnailUri = item->mContentUri;
        item->mScreennailUri = item->mContentUri;
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
        set.addItem(std::move(item));
    }

    set.sortItemsByDate();
    set.updateNumExpectedItems();
    set.generateTitle(true);
}

bool MediaStoreDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    if (item == nullptr || item->mId < 0) {
        return false;
    }
    return mClient.readImage(item->mId, bytes);
}

bool MediaStoreDataSource::supportsRegions(const MediaItem *item) const {
    // Asked once a frame while a photo is fullscreen, so it opens nothing.
    return item != nullptr && !item->mContentUri.empty() && RegionDecoder::looksSupported(item->mMimeType);
}

void MediaStoreDataSource::requestRegion(MediaItem *item, int x, int y, int width, int height, int sampleSize,
                                         RegionCallback done) {
    if (item == nullptr || !item->hasFullSize()) {
        done(Bitmap());
        return;
    }
    // A rectangle outside the image is the decoder's to refuse.
    RegionDecoderPtr decoder = mDecoders.get(item->mContentUri);
    if (!decoder) {
        done(Bitmap());
        return;
    }
    // The decode pool already runs this off the render thread.
    done(decoder->decodeRegion(x, y, width, height, sampleSize));
}
