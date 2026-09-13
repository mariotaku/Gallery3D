// Read-only Art Institute of Chicago API source. Albums are departments and themes;
// two requests load categories and covers, then albums load lazily.
// https://api.artic.edu/docs/
#pragma once

#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "core/ByteCache.h"
#include "media/LocalDataSource.h"

class ArticDataSource : public DataSource {
  public:
    // How many categories to put on the wall, and how many covers to show on
    // each stack before the album is opened.
    ArticDataSource(int albums = 12, int coversPerAlbum = 4)
        : mAlbumCount(albums), mCoversPerAlbum(coversPerAlbum) {}

    void loadMediaSets(MediaFeed *feed) override;
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;
    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override;
    void requestItemBytes(MediaItem *item, BytesCallback done) override;
    bool readsBlockOnNetwork() const override {
        return true;
    }
    // The museum serves IIIF, where the rectangle wanted is part of the url, so
    // a tile is one request and the server does the cropping. Every artwork
    // comes from the same server, so the item does not change the answer.
    bool supportsRegions(const MediaItem *item) const override {
        (void)item;
        return true;
    }
    void requestRegion(MediaItem *item, int x, int y, int width, int height, int outWidth, int outHeight,
                       RegionCallback done) override;
    // Default supportsOperation disables delete and rotate.

    // Maps an artwork record to an item, or null if it has no picture.
    std::unique_ptr<MediaItem> makeItem(const nlohmann::json &artwork) const;

  private:
    // Downloaded JPEG byte budget.
    static constexpr size_t kImageCacheBudget = 32 * 1024 * 1024;

    // Category and pagination cursor (year, id). The API rejects from + limit > 1000,
    // so pages continue after the last sort key instead of using offsets.
    struct Album {
        std::string categoryId;  // "PC-13"
        std::string title;
        int total = 0;  // artworks in it that have a picture
        // Where the last page ended. Unset until one has.
        bool started = false;
        int lastYear = 0;
        int64_t lastId = 0;
        bool exhausted = false;
    };

    // An album and the covers that came back with it, in the one response.
    struct AlbumPage {
        Album album;
        std::vector<std::unique_ptr<MediaItem>> covers;
    };

    // The two requests that make the whole first page. Answers through the
    // callback, which may run before this returns or long after.
    void fetchAlbums(MediaFeed *feed, std::function<void(std::vector<AlbumPage>)> done);

    // The next page of one category, in date order, continuing after `album`'s
    // position.
    void fetchArtworks(MediaFeed *feed, const Album &album, int limit,
                       std::function<void(std::vector<std::unique_ptr<MediaItem>>)> done);

    // Turns one artworks/search response into items.
    std::vector<std::unique_ptr<MediaItem>> itemsFromResponse(const nlohmann::json &parsed) const;

    int mAlbumCount;
    int mCoversPerAlbum;

    std::string mIiifBase = "https://www.artic.edu/iiif/2";

    // Which category each set came from, and which sets are finished. Keyed by
    // the set id, because a MediaSet has nowhere to keep this.
    std::map<int64_t, Album> mAlbumsBySet;
    std::set<int64_t> mFullyLoaded;
    std::mutex mAlbumMutex;

    // Bounded URL cache shared by thumbnails, screennails and zoom views.
    ByteCache mImageCache{kImageCacheBudget};
};
