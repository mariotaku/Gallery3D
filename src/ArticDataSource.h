// A data source backed by the Art Institute of Chicago's public API.
//
// Here to prove the DataSource seam carries something that is not a directory:
// no files, no writes, and every byte arriving over the network. It never says
// it can delete or rotate, so the HUD offers neither. That is the interesting
// half, since nothing in the UI knows what an artwork is; it only asks the
// source what is allowed.
//
// An album is an exhibition. The API has several things that could stand in for
// one - departments, galleries, category terms - and exhibitions are the only
// grouping a person actually curated, with a title worth reading. Category
// terms were the obvious candidate and turned out to be eleven thousand mostly
// obscure tags.
//
// The first page fetches covers only, a handful per exhibition in one request.
// Opening an album fetches the rest, which is what the feed's lazy path is for.
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

#include "LocalDataSource.h"

class ArticDataSource : public DataSource {
  public:
    // How many exhibitions to put on the wall, and how many covers to show on
    // each stack before the album is opened.
    ArticDataSource(int albums = 12, int coversPerAlbum = 4)
        : mAlbums(albums), mCoversPerAlbum(coversPerAlbum) {}

    void loadMediaSets(MediaFeed *feed) override;
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;
    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override;
    // supportsOperation is left alone. A museum's catalogue is not ours to
    // delete from, and the default already says no to everything.

  private:
    // Turns a list of artwork ids into items, without touching the feed. The
    // caller decides whether there is enough here to be worth a set, because an
    // album with nothing in it is worse than no album.
    std::vector<std::unique_ptr<MediaItem>> fetchArtworks(MediaFeed *feed, const std::vector<int64_t> &ids,
                                                          size_t limit);

    int mAlbums;
    int mCoversPerAlbum;

    std::string mIiifBase = "https://www.artic.edu/iiif/2";

    // What each album still has to fetch, and which ones are finished. Keyed by
    // the set id, because a MediaSet has nowhere to keep this.
    std::map<int64_t, std::vector<int64_t>> mArtworkIds;
    std::set<int64_t> mFullyLoaded;
    std::mutex mAlbumMutex;

    // Every image, keyed by the url it came from. A thumbnail, a screennail and
    // a zoomed view are three reads of the same picture, and without this each
    // would be its own download.
    std::map<std::string, std::vector<uint8_t>> mImageCache;
    std::mutex mImageCacheMutex;
};
