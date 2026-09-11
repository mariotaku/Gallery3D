// A data source backed by the Art Institute of Chicago's public API.
//
// Here to prove the DataSource seam carries something that is not a directory:
// no files, no writes, and every byte arriving over the network. It never says
// it can delete or rotate, so the HUD offers neither. That is the interesting
// half, since nothing in the UI knows what an artwork is; it only asks the
// source what is allowed.
//
// An album is a category term - one of the museum's own departments, or one of
// the themes its curators group work under. "Arts of Africa", "Photography and
// Media", "Silk Road". There are eleven thousand category terms in all, but
// their subtype sorts them: the hundred or so departments and themes are real
// groupings, and the rest are the tags that describe a single object, like
// "oil paint" or "ibis".
//
// The whole first page costs two requests. The search endpoint passes
// Elasticsearch straight through, so one call carries a terms aggregation over
// category_ids, which ranks every category by how many artworks in it have a
// picture, with a top_hits aggregation nested inside that brings back each
// one's covers in the same response. A second call turns the winning ids into
// titles. Nothing else is fetched before the wall is drawn.
//
// Opening an album fetches the rest of it, which is what the feed's lazy path
// is for.
//
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

#include "ByteCache.h"
#include "LocalDataSource.h"

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
    // a tile is one request and the server does the cropping.
    bool supportsRegions() const override {
        return true;
    }
    void requestRegionBytes(MediaItem *item, int x, int y, int width, int height, int outWidth, int outHeight,
                            BytesCallback done) override;
    // supportsOperation is left alone. A museum's catalogue is not ours to
    // delete from, and the default already says no to everything.

    // One artwork record to one item, or null when it has no picture. Public
    // because the mapping from the api's fields is worth checking on its own,
    // without a network between the record and the item.
    std::unique_ptr<MediaItem> makeItem(const nlohmann::json &artwork) const;

  private:
    // How much of the downloaded jpeg to keep. Enough for a couple of albums
    // being browsed at once, and small next to what the textures themselves
    // take on the card.
    static constexpr size_t kImageCacheBudget = 32 * 1024 * 1024;

    // A category worth putting on the wall, and where the walk through it has
    // got to.
    //
    // The position is the sort key of the last artwork handed over: its year
    // and, to break ties, its id. The next page asks for whatever sorts after
    // that pair. It has to work this way: the api refuses any request where
    // from plus limit passes a thousand, and there are fifty thousand prints.
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

    // The images, keyed by the url they came from. A thumbnail, a screennail
    // and a zoomed view are three reads of the same picture, and without this
    // each would be its own download. Bounded, because an album is a hundred
    // artworks and the wall holds twelve of them.
    ByteCache mImageCache{kImageCacheBudget};
};
