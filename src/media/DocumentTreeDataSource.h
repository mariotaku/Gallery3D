// The photos in one folder tree the user granted, such as a folder on an SD
// card or in a cloud provider, one album per folder that holds photos.
//
// Scoped storage gives an app no path to walk outside the media store. A tree
// uri from the storage access framework is how the user hands one over, and
// every read goes back through it. The tree is reached through a
// DocumentTreeClient, the Java bridge on Android and a fake in the tests.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "graphics/RegionDecoder.h"
#include "media/DocumentTreeClient.h"
#include "media/LocalDataSource.h"

class DocumentTreeDataSource : public DataSource {
  public:
    DocumentTreeDataSource(DocumentTreeClient &client, std::string treeUri)
        : mClient(client), mTreeUri(std::move(treeUri)) {}

    // Walks the tree a folder at a time and hands each album to the feed as
    // soon as its folder is listed. Stops early when the feed shuts down.
    void loadMediaSets(MediaFeed *feed) override;
    // loadMediaSets fills every set, so this only reports that there is no
    // next page.
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;

    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override;

    // Finds the photo's rotation, date and size, once, and hands them to the
    // render thread as late details. The listing leaves them out, since
    // opening every photo in a large tree before the wall shows takes seconds.
    //
    // A rotation the provider reports with its thumbnails is taken over the
    // EXIF's, and spares opening the photo, which a cloud provider downloads:
    // a thumbnail load learns it in readThumbnail, and a cached thumbnail from
    // a small thumbnail asked for here. The EXIF is read for the rest, and for
    // the whole photo's size.
    void prepareItem(MediaItem *item, ItemLoad load) override;

    // The provider's thumbnail, near maxEdge rather than at it, in the
    // orientation the photo is stored in. False when the provider makes none.
    bool readThumbnail(MediaItem *item, int maxEdge, Bitmap *bitmap) override;

    // BitmapRegionDecoder reads the same document uri.
    bool supportsRegions(const MediaItem *item) const override;
    void requestRegion(MediaItem *item, int x, int y, int width, int height, int sampleSize,
                       RegionCallback done) override;

  private:
    // What is known of one photo past its listing.
    struct Known {
        bool exifRead = false;
        // The rotation came from the provider, and the EXIF leaves it be.
        bool providerRotation = false;
        float rotation = 0.0f;
        int64_t dateTakenMs = 0;
        int width = 0;
        int height = 0;
    };

    Known knownFor(int64_t id);
    // Reads the EXIF unless it has been read, and returns what is known.
    Known readExifOnce(MediaItem *item);
    void takeProviderRotation(MediaItem *item, int degrees);
    // Called holding mKnownMutex, so fills from two loader threads do not
    // interleave.
    static void publish(MediaItem *item, const Known &known);

    DocumentTreeClient &mClient;
    const std::string mTreeUri;
    RegionDecoderCache mDecoders;
    // By item id. The thumbnail and the screennail of one photo load on
    // different threads.
    std::unordered_map<int64_t, Known> mKnown;
    std::mutex mKnownMutex;
};
