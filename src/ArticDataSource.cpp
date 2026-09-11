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
// A department can hold fifty thousand records. The wall is not a catalogue
// browser, and asking for all of them is a long wait for a page nobody
// scrolls to the end of.
const int kMaxItemsPerAlbum = 100;

const char *const kArtworkFields = "id,title,image_id,artist_title,date_end";

size_t appendToVector(void *data, size_t size, size_t count, void *userData) {
    size_t total = size * count;
    std::vector<uint8_t> *out = (std::vector<uint8_t> *)userData;
    const uint8_t *bytes = (const uint8_t *)data;
    out->insert(out->end(), bytes, bytes + total);
    return total;
}

// One curl handle per thread, kept open for the life of it.
//
// This matters more than it looks. A fresh handle means a fresh DNS lookup and
// a fresh TLS handshake for every single image, and the texture threads fetch
// hundreds. Reusing the handle keeps the connection, the TLS session and the
// resolved address, so the second image onward costs one round trip instead of
// four. curl_easy_reset clears the options and keeps all of that.
CURL *threadHandle() {
    struct Holder {
        CURL *handle = curl_easy_init();
        ~Holder() {
            if (handle != nullptr) {
                curl_easy_cleanup(handle);
            }
        }
    };
    static thread_local Holder holder;
    if (holder.handle != nullptr) {
        curl_easy_reset(holder.handle);
    }
    return holder.handle;
}

// One blocking GET. Every caller is already on a worker thread, so blocking
// here is the point rather than a problem.
bool fetch(const std::string &url, std::vector<uint8_t> *out) {
    CURL *handle = threadHandle();
    if (handle == nullptr) {
        return false;
    }
    out->clear();
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendToVector);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    // There are only four texture threads, so a thread parked on a dead
    // connection is a quarter of the pool doing nothing and a wall that stops
    // filling in. Give up on one that has stalled rather than waiting out the
    // whole timeout: under a kilobyte a second for five seconds is a transfer
    // that is not coming back.
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 5L);
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
    // The handle stays; only the header list is ours to free.
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

}  // namespace

std::unique_ptr<MediaItem> ArticDataSource::makeItem(const nlohmann::json &artwork) const {
    std::string imageId = stringOr(artwork, "image_id", "");
    if (imageId.empty()) {
        // Every query here asks for records that have one, but a field can
        // still come back null, and a wall of blanks helps nobody.
        return nullptr;
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
    // The size has to be "!843,843", fit inside that box, rather than "843,"
    // meaning exactly that wide. The server refuses to scale past 100%, so
    // asking for a fixed width is a 403 for every artwork whose original is
    // narrower, and plenty are. "max" and "full" are refused for the same
    // reason.
    item->mContentUri = mIiifBase + "/" + imageId + "/full/!843,843/0/default.jpg";
    item->mThumbnailUri = item->mContentUri;
    item->mScreennailUri = item->mContentUri;
    item->mMimeType = "image/jpeg";
    // The catalogue's year, so the timeline has something to cluster on.
    int year = (int)intOr(artwork, "date_end", 0);
    if (year > 1000 && year < 2100) {
        item->mDateTakenInMs = ((int64_t)(year - 1970)) * 365LL * 24LL * 3600LL * 1000LL;
    }
    return item;
}

std::vector<std::unique_ptr<MediaItem>> ArticDataSource::fetchArtworks(MediaFeed *feed,
                                                                      const std::string &categoryId, int from,
                                                                      int limit) {
    std::vector<std::unique_ptr<MediaItem>> items;
    if (categoryId.empty() || limit <= 0) {
        return items;
    }
    // In this category, and having a picture: both halves of a bool query.
    // Without the second half this pages through records that can never be
    // drawn, and an album comes up short for no visible reason.
    std::string url = std::string(kApi) + "/artworks/search?fields=" + kArtworkFields +
                      "&limit=" + std::to_string(limit) + "&from=" + std::to_string(from) +
                      "&query[bool][must][0][term][category_ids]=" + categoryId +
                      "&query[bool][must][1][exists][field]=image_id";
    nlohmann::json parsed;
    if (!fetchJson(url, &parsed) || !parsed.contains("data")) {
        return items;
    }
    if (feed->isCancelled()) {
        return items;
    }
    for (const nlohmann::json &artwork : parsed["data"]) {
        if (std::unique_ptr<MediaItem> item = makeItem(artwork)) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

std::vector<ArticDataSource::AlbumPage> ArticDataSource::fetchAlbums(MediaFeed *feed) {
    std::vector<AlbumPage> pages;

    // Request one, and the whole wall comes out of it.
    //
    // The terms aggregation ranks the categories by how many artworks in them
    // have a picture. The top_hits nested inside it returns that many artworks
    // per category, with the fields an item needs, so the covers arrive in the
    // same response rather than in a request each. Twelve albums used to be
    // thirteen round trips before anything could be drawn; now it is this one.
    std::string covers = std::to_string(mCoversPerAlbum);
    std::string url = std::string(kApi) +
                      "/artworks/search?limit=0&query[exists][field]=image_id"
                      "&aggs[categories][terms][field]=category_ids"
                      "&aggs[categories][terms][size]=60"
                      "&aggs[categories][aggs][covers][top_hits][size]=" +
                      covers +
                      "&aggs[categories][aggs][covers][top_hits][_source][includes][]=id"
                      "&aggs[categories][aggs][covers][top_hits][_source][includes][]=title"
                      "&aggs[categories][aggs][covers][top_hits][_source][includes][]=image_id"
                      "&aggs[categories][aggs][covers][top_hits][_source][includes][]=artist_title"
                      "&aggs[categories][aggs][covers][top_hits][_source][includes][]=date_end";
    nlohmann::json parsed;
    if (!fetchJson(url, &parsed)) {
        return pages;
    }
    if (parsed.contains("config")) {
        // The IIIF endpoint is advertised rather than assumed. Read before any
        // item is made, since every item's url is built from it.
        mIiifBase = stringOr(parsed["config"], "iiif_url", mIiifBase.c_str());
    }
    auto aggregations = parsed.find("aggregations");
    if (aggregations == parsed.end() || !aggregations->contains("categories")) {
        SDL_Log("artic: no aggregations came back");
        return pages;
    }
    const nlohmann::json &buckets = (*aggregations)["categories"]["buckets"];
    if (!buckets.is_array() || buckets.empty()) {
        return pages;
    }
    if (feed->isCancelled()) {
        return pages;
    }

    std::string ids;
    for (const nlohmann::json &bucket : buckets) {
        std::string key = stringOr(bucket, "key", "");
        if (key.empty()) {
            continue;
        }
        if (!ids.empty()) {
            ids += ",";
        }
        ids += key;
    }
    if (ids.empty()) {
        return pages;
    }

    // Request two: what those ids are called, and which of them are a grouping
    // rather than a label on one object. Most of the eleven thousand terms
    // describe a material or a subject. The departments and themes are the ones
    // somebody curated, and the only ones worth a stack.
    url = std::string(kApi) + "/category-terms?limit=100&fields=id,title,subtype&ids=" + ids;
    nlohmann::json terms;
    if (!fetchJson(url, &terms) || !terms.contains("data")) {
        return pages;
    }
    std::map<std::string, std::string> titles;
    for (const nlohmann::json &term : terms["data"]) {
        std::string subtype = stringOr(term, "subtype", "");
        if (subtype != "department" && subtype != "theme") {
            continue;
        }
        std::string id = stringOr(term, "id", "");
        if (!id.empty()) {
            titles[id] = stringOr(term, "title", "Untitled");
        }
    }

    // Back in the aggregation's order, so the fullest categories come first.
    for (const nlohmann::json &bucket : buckets) {
        if ((int)pages.size() >= mAlbumCount) {
            break;
        }
        std::string key = stringOr(bucket, "key", "");
        auto title = titles.find(key);
        if (title == titles.end()) {
            continue;
        }
        auto hits = bucket.find("covers");
        if (hits == bucket.end()) {
            continue;
        }
        AlbumPage page;
        for (const nlohmann::json &hit : (*hits)["hits"]["hits"]) {
            auto source = hit.find("_source");
            if (source == hit.end()) {
                continue;
            }
            if (std::unique_ptr<MediaItem> item = makeItem(*source)) {
                page.covers.push_back(std::move(item));
            }
        }
        if (page.covers.empty()) {
            // A category with no cover cannot be drawn as a stack.
            continue;
        }
        page.album.categoryId = key;
        page.album.title = title->second;
        page.album.total = (int)intOr(bucket, "doc_count", 0);
        pages.push_back(std::move(page));
    }
    return pages;
}

void ArticDataSource::loadMediaSets(MediaFeed *feed) {
    // curl's global state is not safe to set up lazily from several threads,
    // and this is the first thing that touches it.
    static std::once_flag curlOnce;
    std::call_once(curlOnce, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    std::vector<AlbumPage> pages = fetchAlbums(feed);
    if (pages.empty()) {
        SDL_Log("artic: no categories came back");
        return;
    }

    int64_t setId = 0;
    for (AlbumPage &page : pages) {
        if (feed->isCancelled()) {
            return;
        }
        // A category id is a string and a set wants a number, so the sets are
        // numbered as they are made. Nothing outside here reads the number, and
        // mAlbumsBySet maps it back when the album is opened.
        ++setId;
        MediaSet *set = feed->addMediaSet(setId, this);
        set->mName = page.album.title;
        set->mIsLocal = false;
        for (std::unique_ptr<MediaItem> &cover : page.covers) {
            set->addItem(std::move(cover));
        }
        {
            std::lock_guard<std::mutex> lock(mAlbumMutex);
            mAlbumsBySet[setId] = page.album;
        }
        // How many the category actually holds. Only the first hundred are
        // ever fetched, but a stack labelled 100 tells you nothing and makes
        // every department on the wall look the same size, when one of them has
        // fifty thousand works in it and another has a few hundred.
        set->setNumExpectedItems(page.album.total);
        set->generateTitle(true);
        // Published as they are made. They all came out of one response, so
        // this is the wall appearing rather than filling in.
        feed->updateListener(true);
    }
    SDL_Log("artic: %d categories on the wall", (int)pages.size());
}

void ArticDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    if (parentSet == nullptr) {
        return;
    }
    Album album;
    {
        std::lock_guard<std::mutex> lock(mAlbumMutex);
        if (mFullyLoaded.count(parentSet->mId) != 0) {
            return;
        }
        auto found = mAlbumsBySet.find(parentSet->mId);
        if (found == mAlbumsBySet.end()) {
            return;
        }
        album = found->second;
        mFullyLoaded.insert(parentSet->mId);
    }

    // The covers are already here, so start past them.
    const int have = parentSet->getNumItems();
    const int wanted = std::min(kMaxItemsPerAlbum, album.total) - have;
    if (wanted <= 0) {
        return;
    }
    std::vector<std::unique_ptr<MediaItem>> items = fetchArtworks(feed, album.categoryId, have, wanted);
    int added = (int)items.size();
    for (std::unique_ptr<MediaItem> &item : items) {
        parentSet->addItem(std::move(item));
    }
    // Not updateNumExpectedItems: that would set the count to what is loaded
    // and the label would fall from the category's real size to a hundred the
    // moment the album opened. The count stays what the catalogue says.
    parentSet->generateTitle(true);
    SDL_Log("artic: %s filled in with %d more of %d", parentSet->mName.c_str(), added, album.total);
}

bool ArticDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    if (item == nullptr || item->mContentUri.empty()) {
        return false;
    }
    if (mImageCache.get(item->mContentUri, bytes)) {
        return true;
    }

    std::vector<uint8_t> downloaded;
    if (!fetch(item->mContentUri, &downloaded) || downloaded.empty()) {
        return false;
    }
    mImageCache.put(item->mContentUri, downloaded);
    *bytes = std::move(downloaded);
    return true;
}
