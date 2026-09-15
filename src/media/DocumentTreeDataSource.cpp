#include "media/DocumentTreeDataSource.h"

#include <memory>
#include <unordered_set>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "core/JsonValue.h"
#include "core/StableId.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"

namespace {

nlohmann::json parse(const std::string &text, const char *what) {
    if (text.empty()) {
        return nlohmann::json::array();
    }
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        SDL_Log("Could not read the %s the folder tree returned", what);
        return nlohmann::json::array();
    }
    return parsed;
}

// Whole quarter turns only. The other EXIF orientations mirror, which a
// rotation cannot express.
float rotationFor(int64_t degrees) {
    switch (((degrees % 360) + 360) % 360) {
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

}  // namespace

void DocumentTreeDataSource::loadMediaSets(MediaFeed *feed) {
    if (feed == nullptr) {
        return;
    }
    const uint64_t started = SDL_GetTicks();
    const nlohmann::json folders = parse(mClient.listFolders(mTreeUri), "folder list");
    SDL_Log("The folder tree has %d folders of photos, listed in %u ms", (int)folders.size(),
            (unsigned)(SDL_GetTicks() - started));

    int photos = 0;
    for (const nlohmann::json &folder : folders) {
        const std::string folderUri = stringOr(folder, "id", "");
        if (folderUri.empty()) {
            continue;
        }
        auto set = std::make_unique<MediaSet>();
        set->mId = stableIdFor(folderUri);
        set->mDataSource = this;
        set->mName = stringOr(folder, "name", "");
        set->mType = MediaSet::TYPE_FOLDER;
        set->mIsLocal = true;
        loadFolderItems(*set, folderUri);
        // A folder of nothing but RAW files beside their JPEGs cannot happen,
        // but one whose photos all failed to list can.
        if (set->getNumItems() == 0) {
            continue;
        }
        photos += set->getNumItems();
        feed->addMediaSet(std::move(set));
    }
    SDL_Log("Read %d photos from the folder tree in %u ms", photos, (unsigned)(SDL_GetTicks() - started));
    feed->finishLoadingMediaSets();
}

void DocumentTreeDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    if (feed != nullptr) {
        feed->finishLoadingItemsForSet(parentSet);
    }
}

void DocumentTreeDataSource::loadFolderItems(MediaSet &set, const std::string &folderUri) {
    const nlohmann::json photos = parse(mClient.listPhotos(folderUri), "photo list");
    // One folder, so names alone find a RAW and the JPEG or HEIF beside it.
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
        const std::string uri = stringOr(photo, "uri", "");
        const std::string name = stringOr(photo, "name", "");
        if (uri.empty() || kept.count(name) == 0) {
            continue;
        }
        auto item = std::make_unique<MediaItem>();
        item->mId = stableIdFor(uri);
        // No file path, as with the media store: the three uris name the same
        // document and every read goes back through it.
        item->mContentUri = uri;
        item->mThumbnailUri = uri;
        item->mScreennailUri = uri;
        item->mMimeType = stringOr(photo, "mime", "image/jpeg");
        item->mCaption = name;
        item->mRotation = rotationFor(intOr(photo, "orientation", 0));
        item->mFullWidth = (int)intOr(photo, "width", 0);
        item->mFullHeight = (int)intOr(photo, "height", 0);
        const int64_t modifiedMs = intOr(photo, "dateModified", 0);
        item->mDateModifiedInSec = modifiedMs / 1000;
        // A document has no added date. Its modified one is the nearest.
        item->mDateAddedInSec = item->mDateModifiedInSec;
        item->mDateTakenInMs = intOr(photo, "dateTaken", 0);
        if (item->mDateTakenInMs == 0) {
            item->mDateTakenInMs = modifiedMs;
        }
        set.addItem(std::move(item));
    }

    set.sortItemsByDate();
    set.updateNumExpectedItems();
    set.generateTitle(true);
}

bool DocumentTreeDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    if (item == nullptr || item->mContentUri.empty()) {
        return false;
    }
    return mClient.readDocument(item->mContentUri, bytes);
}

bool DocumentTreeDataSource::supportsRegions(const MediaItem *item) const {
    // Asked once a frame while a photo is fullscreen, so it opens nothing.
    return item != nullptr && !item->mContentUri.empty() && RegionDecoder::looksSupported(item->mMimeType);
}

void DocumentTreeDataSource::requestRegion(MediaItem *item, int x, int y, int width, int height, int sampleSize,
                                           RegionCallback done) {
    if (item == nullptr || !item->hasFullSize()) {
        done(Bitmap());
        return;
    }
    RegionDecoderPtr decoder = mDecoders.get(item->mContentUri);
    if (!decoder) {
        done(Bitmap());
        return;
    }
    done(decoder->decodeRegion(x, y, width, height, sampleSize));
}
