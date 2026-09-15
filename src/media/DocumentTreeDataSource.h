// The photos in one folder tree the user granted, such as a folder on an SD
// card or in a cloud provider, one album per folder that holds photos.
//
// Scoped storage gives an app no path to walk outside the media store. A tree
// uri from the storage access framework is how the user hands one over, and
// every read goes back through it. The tree is reached through a
// DocumentTreeClient, the Java bridge on Android and a fake in the tests.
#pragma once

#include <string>

#include "graphics/RegionDecoder.h"
#include "media/DocumentTreeClient.h"
#include "media/LocalDataSource.h"

class DocumentTreeDataSource : public DataSource {
  public:
    DocumentTreeDataSource(DocumentTreeClient &client, std::string treeUri)
        : mClient(client), mTreeUri(std::move(treeUri)) {}

    void loadMediaSets(MediaFeed *feed) override;
    // loadMediaSets fills every set, so this only reports that there is no
    // next page.
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;

    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override;

    // BitmapRegionDecoder reads the same document uri.
    bool supportsRegions(const MediaItem *item) const override;
    void requestRegion(MediaItem *item, int x, int y, int width, int height, int sampleSize,
                       RegionCallback done) override;

  private:
    void loadFolderItems(MediaSet &set, const std::string &folderUri);

    DocumentTreeClient &mClient;
    const std::string mTreeUri;
    RegionDecoderCache mDecoders;
};
