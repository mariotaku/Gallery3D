#include "media/ArticDataSource.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "core/Dates.h"
#include "media/Http.h"
#include "graphics/ImageDecode.h"
#include "core/JsonValue.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"

namespace {

const char *const kApi = "https://api.artic.edu/api/v1";

// API page-size limit; albums load additional pages as the wall scrolls.
const int kItemsPerPage = 100;

// Requested MediaItem fields. thumbnail includes the original dimensions needed for tiling.
const char *const kArtworkFields = "id,title,image_id,artist_title,date_end,thumbnail";

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
        // The API can return a null image field despite the query filter.
        return nullptr;
    }
    auto item = std::make_unique<MediaItem>();
    item->mId = intOr(artwork, "id", 0);
    item->mCaption = stringOr(artwork, "title", "Untitled");
    std::string artist = stringOr(artwork, "artist_title", "");
    if (!artist.empty()) {
        item->mCaption += " - " + artist;
    }
    // Use !843,843 to fit within the box. Fixed widths, max and full can return 403
    // when they require scaling above 100%. mContentUri supplies the image address.
    item->mContentUri = mIiifBase + "/" + imageId + "/full/!843,843/0/default.jpg";
    item->mThumbnailUri = item->mContentUri;
    item->mScreennailUri = item->mContentUri;
    item->mMimeType = "image/jpeg";
    // Original image dimensions for fullscreen zoom.
    if (artwork.contains("thumbnail") && artwork["thumbnail"].is_object()) {
        const nlohmann::json &thumbnail = artwork["thumbnail"];
        item->mFullWidth = (int)intOr(thumbnail, "width", 0);
        item->mFullHeight = (int)intOr(thumbnail, "height", 0);
    }
    // Catalogue year for clustering. Negative means BC; zero means unknown.
    const int year = (int)intOr(artwork, "date_end", 0);
    if (year != 0 && year > -5000 && year < 2100) {
        // Convert the API's BC years (no year zero) to ISO years (1 BC = 0).
        const int isoYear = (year < 0) ? year + 1 : year;
        item->mDateTakenInMs = Dates::startOfYearMs(isoYear);
        // Use January 1 for year-only dates; labels retain year precision.
        item->mDatePrecision = MediaItem::PRECISION_YEAR;
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

void ArticDataSource::fetchArtworks(MediaFeed *feed, const Album &album, int limit,
                                    std::function<void(std::vector<std::unique_ptr<MediaItem>>)> done) {
    if (album.categoryId.empty() || limit <= 0) {
        done({});
        return;
    }

    // Only dated artworks with pictures participate in the date-ordered walk.
    std::string url = std::string(kApi) + "/artworks/search?fields=" + kArtworkFields +
                      "&limit=" + std::to_string(limit) +
                      "&query[bool][filter][0][term][category_ids]=" + album.categoryId +
                      "&query[bool][filter][1][exists][field]=image_id"
                      "&query[bool][filter][2][exists][field]=date_end"
                      // Sort by year then id for an unambiguous pagination boundary.
                      "&sort[0][date_end][order]=asc"
                      "&sort[1][id][order]=asc";

    if (album.started) {
        // Continue after (year, id): date_end > year OR (date_end == year AND id > lastId).
        const std::string year = std::to_string(album.lastYear);
        url += "&query[bool][filter][3][bool][minimum_should_match]=1"
               "&query[bool][filter][3][bool][should][0][range][date_end][gt]=" +
               year + "&query[bool][filter][3][bool][should][1][bool][must][0][term][date_end]=" + year +
               "&query[bool][filter][3][bool][should][1][bool][must][1][range][id][gt]=" +
               std::to_string(album.lastId);
    }

    fetchJson(url, [this, feed, done](bool ok, nlohmann::json parsed) {
        if (!ok || feed->isCancelled()) {
            done({});
            return;
        }
        done(itemsFromResponse(parsed));
    });
}

void ArticDataSource::fetchAlbums(MediaFeed *feed, std::function<void(std::vector<AlbumPage>)> done) {
    // Aggregate categories by artwork count and fetch their covers in the same response.
    const std::string covers = std::to_string(mCoversPerAlbum);
    const std::string url = std::string(kApi) +
                            "/artworks/search?limit=0"
                            // Match the walk's picture and date filters so counts and covers
                            // agree.
                            "&query[bool][filter][0][exists][field]=image_id"
                            "&query[bool][filter][1][exists][field]=date_end"
                            "&aggs[categories][terms][field]=category_ids"
                            "&aggs[categories][terms][size]=60"
                            "&aggs[categories][aggs][covers][top_hits][size]=" +
                            covers +
                            // Order covers as the first items of the album walk.
                            "&aggs[categories][aggs][covers][top_hits][sort][0][date_end][order]=asc"
                            "&aggs[categories][aggs][covers][top_hits][sort][1][id][order]=asc" +
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=id"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=title"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=image_id"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=artist_title"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=date_end"
                            // Original dimensions for tiling cover images.
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=thumbnail";

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

        // Resolve category titles and retain department/theme groupings, excluding object tags.
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
                // The covers are the first page of the walk, so the next one
                // carries on after them rather than fetching them again.
                page.album.started = true;
                page.album.lastYear = Dates::civilFromMs(page.covers.back()->mDateTakenInMs).year;
                page.album.lastId = page.covers.back()->mId;
                page.album.categoryId = key;
                page.album.title = title->second;
                page.album.total = (int)intOr(bucket, "doc_count", 0);
            // Start the sorted walk from the beginning; aggregation covers may appear again.
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
            // Assign numeric set ids; mAlbumsBySet maps them to string category ids.
            ++setId;
            MediaSet *set = feed->addMediaSet(setId, this);
            set->mName = page.album.title;
            set->mIsLocal = false;
            for (std::unique_ptr<MediaItem> &cover : page.covers) {
                set->addItem(std::move(cover));
            }
            set->sortItemsByDate();
            {
                std::lock_guard<std::mutex> lock(mAlbumMutex);
                mAlbumsBySet[setId] = page.album;
            }
            // Show the category's full count, including items not yet fetched.
            set->setNumExpectedItems(page.album.total);
            set->generateTitle(true);
            feed->updateListener(true);
        }
        SDL_Log("artic: %d categories on the wall", (int)pages.size());
        feed->finishLoadingMediaSets();
    });
}

void ArticDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    if (feed == nullptr) {
        return;
    }
    if (parentSet == nullptr) {
        feed->finishLoadingItemsForSet(parentSet);
        return;
    }

    Album album;
    bool haveAlbum = false;
    {
        std::lock_guard<std::mutex> lock(mAlbumMutex);
        auto found = mAlbumsBySet.find(parentSet->mId);
        if (found != mAlbumsBySet.end() && !found->second.exhausted) {
            album = found->second;
            haveAlbum = true;
        }
    }
    if (!haveAlbum) {
        // Nothing left to fetch, and saying so is what lets the feed ask again
        // about some other set.
        feed->finishLoadingItemsForSet(parentSet);
        return;
    }

    const int64_t setId = parentSet->mId;
    const bool firstPage = !album.started;
    fetchArtworks(feed, album, kItemsPerPage,
                  [this, feed, parentSet, setId, firstPage](std::vector<std::unique_ptr<MediaItem>> items) {
                      const int added = (int)items.size();

                      // Save the next-page cursor before transferring the items; recover its
                      // year from the timestamp.
                      int lastYear = 0;
                      int64_t lastId = 0;
                      if (added > 0) {
                          lastYear = Dates::civilFromMs(items.back()->mDateTakenInMs).year;
                          lastId = items.back()->mId;
                      }

                      for (std::unique_ptr<MediaItem> &item : items) {
                          parentSet->addItem(std::move(item));
                      }
                      if (firstPage) {
                          // Sort aggregation covers with the first page before scrolling
                          // begins.
                          parentSet->sortItemsByDate();
                      }
                      // Later pages are already ordered by the server and can be appended.
                      parentSet->generateTitle(true);

                      {
                          std::lock_guard<std::mutex> lock(mAlbumMutex);
                          auto found = mAlbumsBySet.find(setId);
                          if (found != mAlbumsBySet.end()) {
                              if (added > 0) {
                                  found->second.started = true;
                                  found->second.lastYear = lastYear;
                                  found->second.lastId = lastId;
                              }
                              if (added < kItemsPerPage) {
                                  // A short page ends the collection; replace the count with
                                  // the number reached.
                                  found->second.exhausted = true;
                                  parentSet->setNumExpectedItems(parentSet->getNumItems());
                              }
                          }
                      }

                      feed->updateListener(true);
                      feed->finishLoadingItemsForSet(parentSet);
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

void ArticDataSource::requestRegion(MediaItem *item, int x, int y, int width, int height, int outWidth,
                                    int outHeight, RegionCallback done) {
    if (item == nullptr || !item->hasFullSize() || width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0) {
        done(Bitmap());
        return;
    }
    // Clamp to the image: the server returns 502 for rectangles past its edge.
    // The caller sizes the output for the trimmed region.
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x + width > item->mFullWidth) {
        width = item->mFullWidth - x;
    }
    if (y + height > item->mFullHeight) {
        height = item->mFullHeight - y;
    }
    if (width <= 0 || height <= 0) {
        done(Bitmap());
        return;
    }

    // Extract the image id from the content URL's segment between the IIIF base and parameters.
    const std::string &uri = item->mContentUri;
    const size_t idStart = uri.rfind('/', uri.find("/full/") - 1);
    const size_t idEnd = uri.find("/full/");
    if (idEnd == std::string::npos || idStart == std::string::npos || idStart + 1 >= idEnd) {
        done(Bitmap());
        return;
    }
    const std::string imageId = uri.substr(idStart + 1, idEnd - idStart - 1);

    // IIIF region / size / rotation / quality.format. !w,h fits without distortion.
    char parameters[160];
    SDL_snprintf(parameters, sizeof(parameters), "/%d,%d,%d,%d/!%d,%d/0/default.jpg", x, y, width, height, outWidth,
                 outHeight);
    const std::string url = mIiifBase + "/" + imageId + parameters;

    // Keep tiles out of mImageCache to avoid evicting thumbnails; decoded tiles have a texture
    // budget.
    Http::getAsync(url, [done](bool ok, std::vector<uint8_t> bytes) {
        if (!ok || bytes.empty()) {
            done(Bitmap());
            return;
        }
        // The server already sized the tile, so the decode keeps what it sent.
        ImageDecode::decode(std::move(bytes), 0, [done](Bitmap bitmap) { done(std::move(bitmap)); });
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
