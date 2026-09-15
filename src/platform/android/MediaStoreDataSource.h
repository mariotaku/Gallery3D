// The photo library on Android, read from MediaStore.
//
// Stands in for LocalDataSource, which walks a directory tree. Scoped storage
// stops an app from doing that from Android 10 on, and MediaStore is what
// replaces it: one bucket per folder, addressed by content id rather than path.
#pragma once

#include <map>
#include <mutex>
#include <string>

#include "media/LocalDataSource.h"
#include "graphics/RegionDecoder.h"

class MediaStoreDataSource : public DataSource {
  public:
    void loadMediaSets(MediaFeed *feed) override;
    // Every set is filled by loadMediaSets, so this only reports that there is
    // no next page. Loading here as well would add each photo twice.
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;

    // Photos arrive as bytes from the content resolver. There is no path the
    // app may open, so the decoders never see one.
    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override;

    // Zoomed photos are cropped by BitmapRegionDecoder, which reads the same
    // content uri.
    bool supportsRegions(const MediaItem *item) const override;
    void requestRegion(MediaItem *item, int x, int y, int width, int height, int sampleSize,
                       RegionCallback done) override;

  private:
    // Fills a set the feed does not have yet with the bucket's photos.
    void loadBucketItems(MediaSet &set, const std::string &bucketId);

    // MediaStore bucket ids are what the wall's set ids are built from, but a
    // set id is a number and a bucket id is text, so the mapping is kept.
    std::string bucketIdForSet(int64_t setId) const;

    std::map<int64_t, std::string> mBuckets;
    mutable std::mutex mBucketsMutex;
    RegionDecoderCache mDecoders;
};
