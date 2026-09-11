#include "ArticDataSource.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "Dates.h"
#include "Http.h"
#include "JsonValue.h"
#include "MediaFeed.h"
#include "MediaItem.h"
#include "MediaSet.h"

namespace {

const char *const kApi = "https://api.artic.edu/api/v1";

// How many to fetch at a time, which is also as many as the api will hand over
// at once. A department can hold fifty thousand records, so the wall walks it a
// page at a time as it is scrolled rather than trying to hold the lot.
const int kItemsPerPage = 100;

// The fields an artwork needs to become a MediaItem. Asking for only these
// keeps the responses small enough to parse without noticing.
// thumbnail is asked for because of its width and height, which are the
// original's own, not the thumbnail's. The tiled fullscreen view needs the size
// of the picture before it can work out which part of it is on screen, and this
// way it costs nothing: the field rides along with the search that was being
// made anyway, rather than a second request per artwork to iiif info.json.
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
    // The original's pixels. A Seurat here is 9310 by 6237, against the 843 the
    // wall asks for, so the zoomed view has somewhere to go.
    if (artwork.contains("thumbnail") && artwork["thumbnail"].is_object()) {
        const nlohmann::json &thumbnail = artwork["thumbnail"];
        item->mFullWidth = (int)intOr(thumbnail, "width", 0);
        item->mFullHeight = (int)intOr(thumbnail, "height", 0);
    }
    // The catalogue's year, so the timeline has something to cluster on.
    //
    // Negative is BC and there are a couple of thousand of those here with
    // pictures - a coffin from 889 BC, a stela from 1877 BC. They used to be
    // excluded along with everything before the year 1000, back when a date
    // this old could not be carried or printed.
    //
    // Zero stays out: the api gives no year rather than the year 1 BC when it
    // does not know, and that is also what a missing field reads as.
    const int year = (int)intOr(artwork, "date_end", 0);
    if (year != 0 && year > -5000 && year < 2100) {
        // The api counts BC the way people write it, with no year zero:
        // date_end -889 is the 889 BC on the label of a coffin from the reign
        // of Osorkon I. ISO does have a year zero, where 1 BC is 0, so a BC
        // year is one further along the ISO line than its number.
        //
        // Everything downstream speaks ISO, so the conversion belongs here
        // rather than at each place a date is read.
        const int isoYear = (year < 0) ? year + 1 : year;
        item->mDateTakenInMs = Dates::startOfYearMs(isoYear);
        // The first of January, because the timestamp has to name some day.
        // Nobody knows the month, and the label says so.
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

    // In this category, having a picture, and having a date.
    //
    // The picture is obvious. The date is what the walk is ordered by, so an
    // artwork without one has no place in the sequence - and a sort puts those
    // at the end where the position cannot name them. It costs a little:
    // thirty four of the fifty thousand prints are undated. The count corrects
    // itself when the walk runs out.
    std::string url = std::string(kApi) + "/artworks/search?fields=" + kArtworkFields +
                      "&limit=" + std::to_string(limit) +
                      "&query[bool][filter][0][term][category_ids]=" + album.categoryId +
                      "&query[bool][filter][1][exists][field]=image_id"
                      "&query[bool][filter][2][exists][field]=date_end"
                      // Oldest first, with the id breaking ties so the order is
                      // total. A sort that leaves ties unordered cannot be
                      // paged: the boundary is ambiguous and rows fall through
                      // it.
                      "&sort[0][date_end][order]=asc"
                      "&sort[1][id][order]=asc";

    if (album.started) {
        // Everything that sorts after the last one seen:
        //
        //   date_end > year  OR  (date_end == year AND id > lastId)
        //
        // The second half is what makes this exact. Asking only for a later
        // year drops every artwork sharing the boundary year - a page of five
        // was seen moving the remaining count by thirty nine, and those thirty
        // four went without a word.
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
    // Request one, and the whole wall comes out of it.
    //
    // The terms aggregation ranks the categories by how many artworks in them
    // have a picture. The top_hits nested inside it returns that many artworks
    // per category, with the fields an item needs, so the covers arrive in the
    // same response rather than in a request each. Twelve albums used to be
    // thirteen round trips before anything could be drawn; now it is this one.
    const std::string covers = std::to_string(mCoversPerAlbum);
    const std::string url = std::string(kApi) +
                            "/artworks/search?limit=0"
                            // Having a picture, and having a date. The walk
                            // through an album is ordered by date and so
                            // excludes the undated; counting them here would
                            // make the stack promise more than it can reach,
                            // and a cover without a date sorts to the end of
                            // the first page and leaves the album out of order
                            // at that one seam.
                            "&query[bool][filter][0][exists][field]=image_id"
                            "&query[bool][filter][1][exists][field]=date_end"
                            "&aggs[categories][terms][field]=category_ids"
                            "&aggs[categories][terms][size]=60"
                            "&aggs[categories][aggs][covers][top_hits][size]=" +
                            covers +
                            // In the same order the album is walked in, so the
                            // covers are its first few rather than an arbitrary
                            // handful from the middle. Without this the stack
                            // showed work from anywhere in the collection, and
                            // opening it put those alongside the oldest with a
                            // five hundred year step between them.
                            "&aggs[categories][aggs][covers][top_hits][sort][0][date_end][order]=asc"
                            "&aggs[categories][aggs][covers][top_hits][sort][1][id][order]=asc" +
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=id"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=title"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=image_id"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=artist_title"
                            "&aggs[categories][aggs][covers][top_hits][_source][includes][]=date_end"
                            // Carries the original's width and height, which is
                            // what the tiled fullscreen view lays its grid out
                            // over. Without it a cover opens to the screennail
                            // and stays there, while everything else in the
                            // same album sharpens.
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
                // The covers are the first page of the walk, so the next one
                // carries on after them rather than fetching them again.
                page.album.started = true;
                page.album.lastYear = Dates::civilFromMs(page.covers.back()->mDateTakenInMs).year;
                page.album.lastId = page.covers.back()->mId;
                page.album.categoryId = key;
                page.album.title = title->second;
                page.album.total = (int)intOr(bucket, "doc_count", 0);
            // The covers came out of a top_hits aggregation rather than the
            // sorted walk, so the walk still starts from the beginning. The
            // first few artworks appear twice for it, which the wall shows as
            // the stack's covers and then again in the album.
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
            set->sortItemsByDate();
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

                      // Where the next page picks up, read before the items are
                      // given away. The year comes back out of the timestamp
                      // rather than being carried separately: it went in as the
                      // first of January of that year and comes out the same.
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
                          // Once, and only now. The stack's covers came from an
                          // aggregation rather than from the walk, so they sit
                          // wherever they happened to be picked and the first
                          // page starts back at the oldest. Sorting here puts
                          // that right while nothing has been scrolled yet.
                          parentSet->sortItemsByDate();
                      }
                      // Every page after is appended rather than re-sorted. The
                      // server ordered them, so each one is already later than
                      // the last, and nothing already on screen moves.
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
                                  // A short page is the end of the collection.
                                  // The count becomes what was actually
                                  // reached, which is a little under the
                                  // catalogue's total because the undated are
                                  // not in the walk.
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

void ArticDataSource::requestRegionBytes(MediaItem *item, int x, int y, int width, int height, int outWidth,
                                         int outHeight, BytesCallback done) {
    if (item == nullptr || !item->hasFullSize() || width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0) {
        done(false, std::vector<uint8_t>());
        return;
    }
    // Clamp to the picture. The server answers 502 for a rectangle that runs
    // off the edge rather than trimming it, so an edge tile asked for at its
    // full size comes back as a gap. The caller sizes the output for what it
    // actually gets.
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
        done(false, std::vector<uint8_t>());
        return;
    }

    // The image id is the one path segment of the content url the wall already
    // holds, between the iiif base and the parameters. Pulled back out rather
    // than kept alongside, so there is one place that knows how these urls are
    // put together.
    const std::string &uri = item->mContentUri;
    const size_t idStart = uri.rfind('/', uri.find("/full/") - 1);
    const size_t idEnd = uri.find("/full/");
    if (idEnd == std::string::npos || idStart == std::string::npos || idStart + 1 >= idEnd) {
        done(false, std::vector<uint8_t>());
        return;
    }
    const std::string imageId = uri.substr(idStart + 1, idEnd - idStart - 1);

    // region / size / rotation / quality.format, which is the whole of the IIIF
    // image api. "!w,h" fits inside the box without distorting, the same form
    // the whole-picture url uses.
    char parameters[160];
    SDL_snprintf(parameters, sizeof(parameters), "/%d,%d,%d,%d/!%d,%d/0/default.jpg", x, y, width, height, outWidth,
                 outHeight);
    const std::string url = mIiifBase + "/" + imageId + parameters;

    // Not put in mImageCache. A tile is only wanted while one picture is on
    // screen, and a zoomed walk over a large one would push every thumbnail the
    // wall is still showing out of a cache sized for thumbnails. The decoded
    // tiles are the cache, and the texture budget already bounds those.
    Http::getAsync(url, [done](bool ok, std::vector<uint8_t> bytes) {
        if (!ok || bytes.empty()) {
            done(false, std::vector<uint8_t>());
            return;
        }
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
