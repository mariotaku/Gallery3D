#include "ArticDataSource.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <SDL3/SDL.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "JsonValue.h"
#include "MediaFeed.h"
#include "MediaItem.h"
#include "MediaSet.h"

namespace {

const char *const kApi = "https://api.artic.edu/api/v1";
// Who is asking. The docs ask for this and the image server enforces it.
#define kUserAgent "gallery3d-sdl (git@mariotaku.me)"

// The fields an artwork needs to become a MediaItem. Asking for only these
// keeps the responses small enough to parse without noticing.
const char *const kArtworkFields = "id,title,image_id,artist_title,date_end";

size_t appendToVector(void *data, size_t size, size_t count, void *userData) {
    size_t total = size * count;
    std::vector<uint8_t> *out = (std::vector<uint8_t> *)userData;
    const uint8_t *bytes = (const uint8_t *)data;
    out->insert(out->end(), bytes, bytes + total);
    return total;
}

// One blocking GET. Every caller is already on a worker thread, so blocking
// here is the point rather than a problem.
bool fetch(const std::string &url, std::vector<uint8_t> *out) {
    CURL *handle = curl_easy_init();
    if (handle == nullptr) {
        return false;
    }
    out->clear();
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendToVector);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
    // The docs ask callers to identify themselves with AIC-User-Agent, and the
    // image server means it: without that header every IIIF request is a 403,
    // while the json endpoints serve anyone. A plain User-Agent does not do.
    curl_easy_setopt(handle, CURLOPT_USERAGENT, kUserAgent);
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "AIC-User-Agent: " kUserAgent);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);
    curl_slist_free_all(headers);
    if (result != CURLE_OK) {
        SDL_Log("artic: %s: %s", url.c_str(), curl_easy_strerror(result));
        return false;
    }
    if (status != 200) {
        SDL_Log("artic: %s: HTTP %ld", url.c_str(), status);
        return false;
    }
    return true;
}

bool fetchJson(const std::string &url, nlohmann::json *out) {
    std::vector<uint8_t> response;
    if (!fetch(url, &response)) {
        return false;
    }
    *out = nlohmann::json::parse(response.begin(), response.end(), nullptr, false);
    if (out->is_discarded()) {
        SDL_Log("artic: could not parse %s", url.c_str());
        return false;
    }
    return true;
}

std::string join(const std::vector<int64_t> &values, size_t from, size_t count) {
    std::string out;
    for (size_t i = from; i < values.size() && i < from + count; ++i) {
        if (!out.empty()) {
            out += ",";
        }
        out += std::to_string(values[i]);
    }
    return out;
}

}  // namespace

std::vector<std::unique_ptr<MediaItem>> ArticDataSource::fetchArtworks(MediaFeed *feed,
                                                                      const std::vector<int64_t> &ids,
                                                                      size_t limit) {
    std::vector<std::unique_ptr<MediaItem>> items;
    if (ids.empty() || limit == 0) {
        return items;
    }
    // The API takes a comma separated id list, which is one request instead of
    // one per artwork.
    std::string url = std::string(kApi) + "/artworks?limit=100&fields=" + kArtworkFields +
                      "&ids=" + join(ids, 0, limit);
    nlohmann::json parsed;
    if (!fetchJson(url, &parsed) || !parsed.contains("data")) {
        return items;
    }
    if (feed->isCancelled()) {
        return items;
    }

    for (const nlohmann::json &artwork : parsed["data"]) {
        std::string imageId = stringOr(artwork, "image_id", "");
        if (imageId.empty()) {
            // Not every record has a picture, and a wall of blanks helps nobody.
            continue;
        }
        auto item = std::make_unique<MediaItem>();
        item->mId = intOr(artwork, "id", 0);
        item->mCaption = stringOr(artwork, "title", "Untitled");
        std::string artist = stringOr(artwork, "artist_title", "");
        if (!artist.empty()) {
            item->mCaption += " - " + artist;
        }
        // No path, which is the whole point. mContentUri is how the item is
        // addressed and readItemBytes is what turns it into pixels.
        //
        // The size has to be "!843,843", fit inside that box, rather than
        // "843," meaning exactly that wide. The server refuses to scale past
        // 100%, so asking for a fixed width is a 403 for every artwork whose
        // original is narrower, and plenty are. "max" and "full" are refused
        // for the same reason.
        item->mContentUri = mIiifBase + "/" + imageId + "/full/!843,843/0/default.jpg";
        item->mThumbnailUri = item->mContentUri;
        item->mScreennailUri = item->mContentUri;
        item->mMimeType = "image/jpeg";
        // The catalogue's year, so the timeline has something to cluster on.
        int year = (int)intOr(artwork, "date_end", 0);
        if (year > 1000 && year < 2100) {
            item->mDateTakenInMs = ((int64_t)(year - 1970)) * 365LL * 24LL * 3600LL * 1000LL;
        }
        items.push_back(std::move(item));
    }
    return items;
}

void ArticDataSource::loadMediaSets(MediaFeed *feed) {
    // curl's global state is not safe to set up lazily from several threads,
    // and this is the first thing that touches it.
    static std::once_flag curlOnce;
    std::call_once(curlOnce, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    // Ask for more than are wanted, because plenty of exhibitions have no
    // artworks attached and are no use as an album.
    std::string url = std::string(kApi) + "/exhibitions?limit=100&fields=id,title,artwork_ids";
    nlohmann::json parsed;
    if (!fetchJson(url, &parsed) || !parsed.contains("data")) {
        return;
    }
    if (parsed.contains("config")) {
        // The IIIF endpoint is advertised rather than assumed.
        mIiifBase = stringOr(parsed["config"], "iiif_url", mIiifBase.c_str());
    }

    int albums = 0;
    for (const nlohmann::json &exhibition : parsed["data"]) {
        if (feed->isCancelled() || albums >= mAlbums) {
            return;
        }
        auto ids = exhibition.find("artwork_ids");
        if (ids == exhibition.end() || !ids->is_array() || ids->size() < 4) {
            continue;
        }
        std::vector<int64_t> artworkIds;
        for (const nlohmann::json &id : *ids) {
            if (id.is_number_integer()) {
                artworkIds.push_back(id.get<int64_t>());
            }
        }
        if (artworkIds.empty()) {
            continue;
        }

        // Covers first, and the set only once they are in hand. An exhibition
        // whose artworks have no pictures must not become a set at all: an
        // empty album reaches the draw code as a stack with no cover, and that
        // is a crash rather than a blank.
        std::vector<std::unique_ptr<MediaItem>> covers =
            fetchArtworks(feed, artworkIds, (size_t)mCoversPerAlbum);
        if (covers.empty()) {
            continue;
        }

        int64_t setId = intOr(exhibition, "id", 0);
        MediaSet *set = feed->addMediaSet(setId, this);
        set->mName = stringOr(exhibition, "title", "Untitled exhibition");
        set->mIsLocal = false;
        for (std::unique_ptr<MediaItem> &cover : covers) {
            set->addItem(std::move(cover));
        }
        {
            std::lock_guard<std::mutex> lock(mAlbumMutex);
            mArtworkIds[setId] = artworkIds;
        }
        // The stack says how many the exhibition holds, not how many have been
        // fetched, so the count does not jump when the album is opened.
        set->setNumExpectedItems((int)artworkIds.size());
        set->generateTitle(true);
        ++albums;
        // Published as they arrive, so the wall fills in rather than staying
        // empty until every exhibition is in.
        feed->updateListener(true);
    }
    SDL_Log("artic: %d exhibitions on the wall", albums);
}

void ArticDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    if (parentSet == nullptr) {
        return;
    }
    std::vector<int64_t> ids;
    {
        std::lock_guard<std::mutex> lock(mAlbumMutex);
        if (mFullyLoaded.count(parentSet->mId) != 0) {
            return;
        }
        auto found = mArtworkIds.find(parentSet->mId);
        if (found == mArtworkIds.end()) {
            return;
        }
        ids = found->second;
        mFullyLoaded.insert(parentSet->mId);
    }

    // The covers are already here, so start past them. 100 is the API's page
    // size and plenty for one wall.
    size_t have = (size_t)parentSet->getNumItems();
    if (have >= ids.size()) {
        return;
    }
    std::vector<int64_t> remaining(ids.begin() + (long)have, ids.end());
    std::vector<std::unique_ptr<MediaItem>> items = fetchArtworks(feed, remaining, 100);
    int added = (int)items.size();
    for (std::unique_ptr<MediaItem> &item : items) {
        parentSet->addItem(std::move(item));
    }
    parentSet->updateNumExpectedItems();
    parentSet->generateTitle(true);
    SDL_Log("artic: %s filled in with %d more", parentSet->mName.c_str(), added);
}

bool ArticDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    if (item == nullptr || item->mContentUri.empty()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mImageCacheMutex);
        auto cached = mImageCache.find(item->mContentUri);
        if (cached != mImageCache.end()) {
            *bytes = cached->second;
            return true;
        }
    }

    std::vector<uint8_t> downloaded;
    if (!fetch(item->mContentUri, &downloaded) || downloaded.empty()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mImageCacheMutex);
        mImageCache[item->mContentUri] = downloaded;
    }
    *bytes = std::move(downloaded);
    return true;
}
