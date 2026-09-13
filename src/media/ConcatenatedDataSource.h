// Port of com.cooliris.media.ConcatenatedDataSource: enumerates two sources in order.
// MediaFeed routes item loads and operations through each set's owning source.
// Pixel and region requests also go directly to that source.
#pragma once

#include "media/LocalDataSource.h"

class ConcatenatedDataSource : public DataSource {
  public:
    ConcatenatedDataSource(DataSource *first, DataSource *second) : mFirst(first), mSecond(second) {}

    void loadMediaSets(MediaFeed *feed) override;
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;
    bool performOperation(int operation, MediaItem *item, const void *data) override;
    bool supportsOperation(int operation) const override;
    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override;
    void requestItemBytes(MediaItem *item, BytesCallback done) override;
    // Whether either source may block on the network; textures route per item.
    bool readsBlockOnNetwork() const override;
    void shutdown() override;

  private:
    DataSource *mFirst;
    DataSource *mSecond;
};
