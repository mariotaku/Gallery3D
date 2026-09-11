// Port of com.cooliris.media.ConcatenatedDataSource: two sources behind one,
// so a feed can show a local library and something else together.
//
// It only has to enumerate. Everything set specific routes itself: each set
// remembers the source that added it, and MediaFeed asks that one for its items
// and its operations. So this class does not have to know which set came from
// where, and adding a third source is another wrapper rather than a change
// here.
//
// The one thing a second source cannot do yet is serve pixels from anywhere but
// the filesystem. MediaItemTexture and FileTexture both read mFilePath through
// SDL_image, so a remote source has to put a file on disk and point mFilePath
// at it. Giving MediaItem a way to hand back bytes instead is the next step,
// and SDL_image can already load from memory.
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
    void shutdown() override;

  private:
    DataSource *mFirst;
    DataSource *mSecond;
};
