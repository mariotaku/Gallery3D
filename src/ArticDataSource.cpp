#include "ArticDataSource.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "Http.h"
#include "JsonValue.h"
#include "MediaFeed.h"
#include "MediaItem.h"
#include "MediaSet.h"

namespace {

const char *const kApi = "https://api.artic.edu/api/v1";

// A department can hold fifty thousand records. The wall is not a catalogue
// browser, and asking for all of them is a long wait for a page nobody
// scrolls to the end of.
const int kMaxItemsPerAlbum = 100;

// The fields an artwork needs to become a MediaItem. Asking for only these
// keeps the responses small enough to parse without noticing.
const char *const kArtworkFields = "id,title,image_id,artist_title,date_end";

// One json request. Like Http::getAsync, the callback runs inline natively and
// from the browser on the web.
void fetchJson(const std::string &url, std::function<void(bool, nlohmann::json)> done) {
    Http::getAsync(url, [url, done](bool ok, std::vector<uint8_t> response) {
        if (!ok || response.empty()) {
            done(false, nlohmann::json());
            return;
        }
        nlohmann::json parsed = nlohmann::json::parse(response.begin(), response.end(), nullptr, false);
        if (parsed.is_discarded()) {
            SDL_Log("artic: could not parse %s", url.c_str());
            done(false, nlohmann::json());
            return;
        }
        done(true, std::move(parsed));
    });
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

std::vector<std::unique_ptr<MediaItem>> ArticDataSource::itemsFromResponse(const nlohmann::json &parsed) const {
    std::vector<std::unique_ptr<MediaItem>> items;
    if (!parsed.contains("data")) {
        return items;
    }
    for (const nlohmann::json &artwork : parsed["data"]) {
        if (std::unique_ptr<MediaItem> item = makeItem(artwork)) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

void ArticDataSource::fetchArtworks(MediaFeed *feed, const std::string &categoryId, int from, int limit,
                                    std::function<void(std::vector<std::unique_ptr<MediaItem>>)> done) {
    if (categoryId.empty() || limit <= 0) {
        done({});
        return;
    }
    // In this category, and having a picture: both halves of a bool query.
    // Without the second half this pages through records that can never be
    // drawn, and an album comes up short for no visible reason.
    const std::string url = std::string(kApi) + "/artworks/search?fields=" + kArtworkFields +
                            "&limit=" + std::to_string(limit) + "&from=" + std::to_string(from) +
                            "&query[bool][must][0][term][category_ids]=" + categoryId +
                            "&query[bool][must][1][exists][field]=image_id";
    fetchJson(url, [this, feed, done](bool ok, nlohmann::json parsed) {
        if (!ok || feed->isCancelled()) {
            done({});
            return;
        }
        done(itemsFromResponse(parsed));
    });
}

void ArticDataSource::fetchAlbums(MediaFeed *feed, std::function<void(std::vector<AlbumPage>)> done) {
    // Request one, and the whole wall comes out of it.
    //
    // The terms aggregation ranks the categories by how many artworks in them
    // have a picture. The top_hits nested inside it returns that many artworks
    // per category, with the fields an item needs, so the covers arrive in the
    // same response rather than in a request each. Twelve albums used to be
    // thirteen round trips before anything could be drawn; now it is this one.
    const std::string covers = std::to_string(mCoversPerAlbum);
    const std::string url = std::string(kApi) +
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

    fetchJson(url, [this, feed, done](bool ok, nlohmann::json parsed) {
        if (!ok) {
            done({});
            return;
        }
        if (parsed.contains("config")) {
            // The IIIF endpoint is advertised rather than assumed. Read before
            // any item is made, since every item's url is built from it.
            mIiifBase = stringOr(parsed["config"], "iiif_url", mIiifBase.c_str());
        }
        auto aggregations = parsed.find("aggregations");
        if (aggregations == parsed.end() || !aggregations->contains("categories")) {
            SDL_Log("artic: no aggregations came back");
            done({});
            return;
        }
        if (feed->isCancelled()) {
            done({});
            return;
        }
        // Copied out, because the response goes away with this callback and the
        // next one needs the buckets.
        auto buckets = std::make_shared<nlohmann::json>((*aggregations)["categories"]["buckets"]);
        if (!buckets->is_array() || buckets->empty()) {
            done({});
            return;
        }

        std::string ids;
        for (const nlohmann::json &bucket : *buckets) {
            const std::string key = stringOr(bucket, "key", "");
            if (key.empty()) {
                continue;
            }
            if (!ids.empty()) {
                ids += ",";
            }
            ids += key;
        }
        if (ids.empty()) {
            done({});
            return;
        }

        // Request two: what those ids are called, and which of them are a
        // grouping rather than a label on one object. Most of the eleven
        // thousand terms describe a material or a subject. The departments and
        // themes are the ones somebody curated, and the only ones worth a
        // stack.
        const std::string termsUrl =
            std::string(kApi) + "/category-terms?limit=100&fields=id,title,subtype&ids=" + ids;
        fetchJson(termsUrl, [this, buckets, done](bool termsOk, nlohmann::json terms) {
            std::vector<AlbumPage> pages;
            if (!termsOk || !terms.contains("data")) {
                done(std::move(pages));
                return;
            }
            std::map<std::string, std::string> titles;
            for (const nlohmann::json &term : terms["data"]) {
                const std::string subtype = stringOr(term, "subtype", "");
                if (subtype != "department" && subtype != "theme") {
                    continue;
                }
                const std::string id = stringOr(term, "id", "");
                if (!id.empty()) {
                    titles[id] = stringOr(term, "title", "Untitled");
                }
            }

            // Back in the aggregation's order, so the fullest categories come
            // first.
            for (const nlohmann::json &bucket : *buckets) {
                if ((int)pages.size() >= mAlbumCount) {
                    break;
                }
                const std::string key = stringOr(bucket, "key", "");
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
            done(std::move(pages));
        });
    });
}

void ArticDataSource::loadMediaSets(MediaFeed *feed) {
    fetchAlbums(feed, [this, feed](std::vector<AlbumPage> pages) {
        if (pages.empty()) {
            SDL_Log("artic: no categories came back");
            feed->finishLoadingMediaSets();
            return;
        }

        int64_t setId = 0;
        for (AlbumPage &page : pages) {
            if (feed->isCancelled()) {
                return;
            }
            // A category id is a string and a set wants a number, so the sets
            // are numbered as they are made. Nothing outside here reads the
            // number, and mAlbumsBySet maps it back when the album is opened.
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
            // ever fetched, but a stack labelled 100 tells you nothing and
            // makes every department on the wall look the same size, when one
            // of them has fifty thousand works in it.
            set->setNumExpectedItems(page.album.total);
            set->generateTitle(true);
            feed->updateListener(true);
        }
        SDL_Log("artic: %d categories on the wall", (int)pages.size());
        feed->finishLoadingMediaSets();
    });
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
    const std::string name = parentSet->mName;
    const int total = album.total;
    fetchArtworks(feed, album.categoryId, have, wanted,
                  [feed, parentSet, name, total](std::vector<std::unique_ptr<MediaItem>> items) {
                      const int added = (int)items.size();
                      for (std::unique_ptr<MediaItem> &item : items) {
                          parentSet->addItem(std::move(item));
                      }
                      // Not updateNumExpectedItems: that would set the count to
                      // what is loaded and the label would fall from the
                      // category's real size to a hundred the moment the album
                      // opened.
                      parentSet->generateTitle(true);
                      SDL_Log("artic: %s filled in with %d more of %d", name.c_str(), added, total);
                      feed->updateListener(true);
                  });
}

void ArticDataSource::requestItemBytes(MediaItem *item, BytesCallback done) {
    if (item == nullptr || item->mContentUri.empty()) {
        done(false, std::vector<uint8_t>());
        return;
    }
    std::vector<uint8_t> cached;
    if (mImageCache.get(item->mContentUri, &cached)) {
        done(true, std::move(cached));
        return;
    }
    const std::string url = item->mContentUri;
    Http::getAsync(url, [this, url, done](bool ok, std::vector<uint8_t> bytes) {
        if (!ok || bytes.empty()) {
            done(false, std::vector<uint8_t>());
            return;
        }
        mImageCache.put(url, bytes);
        done(true, std::move(bytes));
    });
}

bool ArticDataSource::readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
    if (item == nullptr || item->mContentUri.empty()) {
        return false;
    }
    if (mImageCache.get(item->mContentUri, bytes)) {
        return true;
    }

    std::vector<uint8_t> downloaded;
    if (!Http::get(item->mContentUri, &downloaded) || downloaded.empty()) {
        return false;
    }
    mImageCache.put(item->mContentUri, downloaded);
    *bytes = std::move(downloaded);
    return true;
}
