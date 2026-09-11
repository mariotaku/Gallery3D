// Port of com.cooliris.media.ConcatenatedDataSource: two sources behind one,
// so a feed can show a local library and something else together.
//
// It only has to enumerate. Everything set specific routes itself: each set
// remembers the source that added it, and MediaFeed asks that one for its items
// and its operations. So this class does not have to know which set came from
// where, and adding a third source is another wrapper rather than a change
// here.
//
// Pixels route the same way. A source hands back the encoded bytes of an item
// through requestItemBytes, so nothing has to be a file on this disk, and the
// tiled fullscreen view asks the item's own source for a rectangle of the
// original. Both go to the source that made the set, not through here.
#pragma once

#include "LocalDataSource.h"

class ConcatenatedDataSource : public DataSource {
  public:
    ConcatenatedDataSource(DataSource *first, DataSource *second) : mFirst(first), mSecond(second) {}

    void loadMediaSets(MediaFeed *feed) override;
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;
    bool performOperation(int operation, MediaItem *item, const void *data) override;
    bool supportsOperation(int operation) const override;
    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override;
    void requestItemBytes(MediaItem *item, BytesCallback done) override;
    // Asked without an item in hand, so this is "could anything behind here
    // block on the network". A wall can hold sets from both at once, so the
    // routing that matters is per item, in Texture::loadsOverNetwork.
    bool readsBlockOnNetwork() const override;
    void shutdown() override;

  private:
    DataSource *mFirst;
    DataSource *mSecond;
};
