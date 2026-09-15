#include "media/DocumentTreeDataSource.h"

#include <deque>
#include <memory>
#include <unordered_set>
#include <utility>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "core/JsonValue.h"
#include "core/StableId.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"

namespace {

nlohmann::json parseObject(const std::string &text) {
    if (text.empty()) {
        return nlohmann::json();
    }
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    return parsed.is_object() ? parsed : nlohmann::json();
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

// Fills a set with the photos one folder listed.
void fillItems(MediaSet &set, const nlohmann::json &photos) {
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
        const int64_t modifiedMs = intOr(photo, "dateModified", 0);
        item->mDateModifiedInSec = modifiedMs / 1000;
        // A document has no added date. Its modified one is the nearest.
        item->mDateAddedInSec = item->mDateModifiedInSec;
        // Until prepareItem reads the photo's own date, and for the order of
        // the album, which does not change once it is on the wall.
        item->mDateTakenInMs = modifiedMs;
        set.addItem(std::move(item));
    }

    set.sortItemsByDate();
    set.updateNumExpectedItems();
    set.generateTitle(true);
}

}  // namespace

void DocumentTreeDataSource::loadMediaSets(MediaFeed *feed) {
    if (feed == nullptr) {
        return;
    }
    const uint64_t started = SDL_GetTicks();
    uint64_t firstAlbumMs = 0;
    int albums = 0;
    int photos = 0;

    // Breadth first, one query per folder, and each album goes to the feed as
    // soon as its folder is listed: a large tree fills the wall as it is walked
    // rather than after its last folder.
    std::deque<std::pair<std::string, std::string>> pending;
    pending.emplace_back(mTreeUri, std::string());
    std::unordered_set<std::string> visited;
    while (!pending.empty() && !feed->isCancelled()) {
        const std::string folderUri = std::move(pending.front().first);
        std::string name = std::move(pending.front().second);
        pending.pop_front();
        // A provider can list one folder under two parents, or lead back up
        // through a shortcut. Each folder is one album, walked once.
        if (!visited.insert(folderUri).second) {
            continue;
        }

        const nlohmann::json folder = parseObject(mClient.listFolder(folderUri));
        if (!folder.is_object()) {
            if (folderUri == mTreeUri) {
                SDL_Log("Could not read the folder tree %s", mTreeUri.c_str());
            }
            continue;
        }
        if (name.empty()) {
            name = stringOr(folder, "name", "");
        }
        const auto children = folder.find("folders");
        if (children != folder.end() && children->is_array()) {
            for (const nlohmann::json &child : *children) {
                const std::string childUri = stringOr(child, "id", "");
                if (!childUri.empty()) {
                    pending.emplace_back(childUri, stringOr(child, "name", ""));
                }
            }
        }
        const auto listed = folder.find("photos");
        if (listed == folder.end() || !listed->is_array() || listed->empty()) {
            continue;
        }

        auto set = std::make_unique<MediaSet>();
        set->mId = stableIdFor(folderUri);
        set->mDataSource = this;
        set->mName = name;
        set->mType = MediaSet::TYPE_FOLDER;
        set->mIsLocal = true;
        fillItems(*set, *listed);
        if (set->getNumItems() == 0) {
            continue;
        }
        photos += set->getNumItems();
        if (++albums == 1) {
            firstAlbumMs = SDL_GetTicks() - started;
        }
        feed->addMediaSet(std::move(set));
    }
    SDL_Log("Read %d photos in %d albums from the folder tree in %u ms, the first album after %u ms", photos, albums,
            (unsigned)(SDL_GetTicks() - started), (unsigned)firstAlbumMs);
    feed->finishLoadingMediaSets();
}

void DocumentTreeDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    if (feed != nullptr) {
        feed->finishLoadingItemsForSet(parentSet);
    }
}

bool DocumentTreeDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    if (item == nullptr || item->mContentUri.empty()) {
        return false;
    }
    return mClient.readDocument(item->mContentUri, bytes);
}

void DocumentTreeDataSource::prepareItem(MediaItem *item) {
    if (item == nullptr || item->mContentUri.empty()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mPreparedMutex);
        if (!mPrepared.insert(item->mId).second) {
            return;
        }
    }
    const nlohmann::json exif = parseObject(mClient.readExif(item->mContentUri, item->mMimeType));
    if (!exif.is_object()) {
        return;
    }
    MediaItem::LateDetails &details = item->mLateDetails;
    details.rotation = rotationFor(intOr(exif, "orientation", 0));
    details.dateTakenMs = intOr(exif, "dateTaken", 0);
    details.width = (int)intOr(exif, "width", 0);
    details.height = (int)intOr(exif, "height", 0);
    item->mLateDetailsPending.store(true);
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
