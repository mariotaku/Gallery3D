// Port of com.cooliris.media.DataSource and LocalDataSource.
//
// The original read the MediaStore through a ContentResolver and kept a disk
// cache of thumbnails. This one walks a directory tree instead: one MediaSet
// per folder, one MediaItem per image file.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class MediaFeed;
class MediaSet;

class MediaItem;

class DataSource {
  public:
    virtual ~DataSource() = default;
    // Fills the feed with the top level sets. Runs on a worker thread. A source
    // that adds sets should pass itself to MediaFeed::addMediaSet, so anything
    // set specific comes back to it rather than to whichever source the feed
    // was constructed with.
    virtual void loadMediaSets(MediaFeed *feed) = 0;
    // Fills in the items of one set. Runs on a worker thread. A source that
    // filled its sets during loadMediaSets can leave this empty; one that
    // cannot afford to enumerate everything up front does the work here.
    virtual void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) = 0;
    // Carries out an operation on one item, and reports whether it stuck. The
    // feed only updates itself for the ones that did, so a source that cannot
    // do something says so rather than leaving the wall out of step with the
    // storage behind it. Operations are MediaFeed's OPERATION_ constants.
    virtual bool performOperation(int operation, MediaItem *item, const void *data) {
        (void)operation;
        (void)item;
        (void)data;
        return false;
    }

    // Whether this source can carry an operation out at all. The HUD asks
    // before it offers the button: a source with no delete should not show one
    // that fails. Operations are MediaFeed's OPERATION_ constants.
    virtual bool supportsOperation(int operation) const {
        (void)operation;
        return false;
    }

    // Hands back the encoded bytes of an item, for a source whose photos are
    // not files on this disk. Returning false means the item is a local file
    // and mFilePath should be read instead, which is what keeps the local
    // source free of any copying. Called on a loader thread.
    // True when reading an item's bytes goes over the network. Such a read is
    // nearly all waiting rather than work, so it belongs in a pool of its own:
    // a handful can be in flight for what one decode costs in cpu, and a
    // stalled one must not be able to hold up a decode.
    virtual bool readsBlockOnNetwork() const {
        return false;
    }

    // Called with the item's encoded bytes, or with false. May answer before it
    // returns or long after, so the caller has to be written for both.
    using BytesCallback = std::function<void(bool ok, std::vector<uint8_t> bytes)>;

    // The form the texture loader uses. The default runs the blocking read
    // below and answers at once, which is right for anything already on this
    // disk; a source that fetches has something to override.
    virtual void requestItemBytes(MediaItem *item, BytesCallback done) {
        std::vector<uint8_t> bytes;
        const bool ok = readItemBytes(item, &bytes);
        if (done) {
            done(ok, std::move(bytes));
        }
    }

    virtual bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) {
        (void)item;
        (void)bytes;
        return false;
    }

    // Whether this source can hand over one rectangle of an original rather
    // than the whole of it. That is what the tiled fullscreen view is built
    // on: it draws a picture as a grid of pieces and only ever asks for the
    // pieces on screen.
    //
    // Few can. SDL_image decodes a whole file or nothing - there is no
    // sub-rectangle in the api, and the web build's stb backend has none
    // either - so a source whose pictures are files on this disk says no here
    // and the fullscreen view keeps to the screennail it already had.
    //
    // A source that fetches is a different matter. IIIF puts the rectangle in
    // the url, so the museum's server does the cropping and the scaling and
    // sends back a piece the size of a tile. The region decoder is on the
    // other end of the wire.
    virtual bool supportsRegions() const {
        return false;
    }

    // The encoded bytes of the rectangle (x, y, width, height) of the item's
    // original, scaled to outWidth by outHeight. The rectangle is in the
    // original's own pixels, which is what MediaItem::mFullWidth counts.
    //
    // Only called on a source that said yes above. Answers the same way
    // requestItemBytes does: before it returns, or long after.
    virtual void requestRegionBytes(MediaItem *item, int x, int y, int width, int height, int outWidth, int outHeight,
                                    BytesCallback done) {
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)outWidth;
        (void)outHeight;
        (void)item;
        if (done) {
            done(false, std::vector<uint8_t>());
        }
    }

    virtual void shutdown() {}
};

class LocalDataSource : public DataSource {
  public:
    // Matches the original, which gave the camera folder its own bucket id.
    static const int64_t CAMERA_BUCKET_ID = 0;

    explicit LocalDataSource(std::string rootPath) : mRootPath(std::move(rootPath)) {}

    void loadMediaSets(MediaFeed *feed) override;
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;
    bool performOperation(int operation, MediaItem *item, const void *data) override;
    bool supportsOperation(int operation) const override;

    static bool isSupportedImage(const std::string &path);
    static std::string mimeTypeForPath(const std::string &path);

  private:
    struct Folder {
        std::string path;
        std::string name;
        std::vector<std::string> files;
    };

    void scan(const std::string &path, std::vector<Folder> &folders) const;

    std::string mRootPath;
};
