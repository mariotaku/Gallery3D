// Port of com.cooliris.media.DataSource and LocalDataSource.
// Lists the photos under a set of folders, from the platform's index where it
// covers them and by walking the folders where it does not: one MediaSet per
// image folder, one MediaItem per image.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "media/PhotoIndex.h"
#include "graphics/RegionDecoder.h"

class MediaFeed;
class MediaSet;

class MediaItem;

class DataSource {
  public:
    virtual ~DataSource() = default;
    // Enumerates sets on a worker thread. Pass the owning source to addMediaSet
    // so item loads and operations route back to it.
    virtual void loadMediaSets(MediaFeed *feed) = 0;
    // Loads one set's items on a worker thread; may be empty if loadMediaSets filled them.
    virtual void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) = 0;
    // Performs a MediaFeed::OPERATION_ on an item. Return success only when it persisted;
    // the feed updates only successful items.
    virtual bool performOperation(int operation, MediaItem *item, const void *data) {
        (void)operation;
        (void)item;
        (void)data;
        return false;
    }

    // Whether the source supports a MediaFeed::OPERATION_; controls HUD availability.
    virtual bool supportsOperation(int operation) const {
        (void)operation;
        return false;
    }

    // Called with the item's encoded bytes, or with false. May answer before it
    // returns or long after, so the caller has to be written for both.
    using BytesCallback = std::function<void(bool ok, std::vector<uint8_t> bytes)>;

    // Called with a decoded region, or with an invalid Bitmap. Answers inline
    // or later, the same as BytesCallback.
    using RegionCallback = std::function<void(Bitmap bitmap)>;

    // Texture-loader entry point; defaults to an inline blocking read and callback.
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

    // A thumbnail the source already keeps for the item, scaled so its long
    // edge is maxEdge and in the orientation the item's pixels are stored in.
    // False when it has none, and the caller decodes the item's bytes instead.
    // Blocks, like readItemBytes.
    virtual bool readThumbnail(MediaItem *item, int maxEdge, Bitmap *bitmap) {
        (void)item;
        (void)maxEdge;
        (void)bitmap;
        return false;
    }

    // Whether this item can be drawn from cropped regions rather than one
    // downscaled decode. It takes the item because a local source answers per
    // file: only some formats have a region decoder behind them.
    virtual bool supportsRegions(const MediaItem *item) const {
        (void)item;
        return false;
    }

    // Requests a region in original-image pixels, decoded at outWidth by outHeight.
    // Only used with supportsRegions; callback may run inline or later.
    //
    // This hands back pixels rather than encoded bytes so each source can reach
    // them its own way: a local file is cropped straight out of the original,
    // and the media store goes through the platform's region decoder.
    virtual void requestRegion(MediaItem *item, int x, int y, int width, int height, int outWidth, int outHeight,
                               RegionCallback done) {
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)outWidth;
        (void)outHeight;
        (void)item;
        if (done) {
            done(Bitmap());
        }
    }

    virtual void shutdown() {}
};

class LocalDataSource : public DataSource {
  public:
    // What to call a folder on the wall: its own last component. A path that
    // ends in a separator has no filename of its own, and that is how SDL hands
    // out the user's folders, so the separator comes off first.
    static std::string folderDisplayName(const std::string &path);

    explicit LocalDataSource(std::string rootPath) : mRoots{std::move(rootPath)} {}

    // Several folders walked as one library. A folder inside another is walked
    // once, and an album inside any of cameraRolls is marked as the camera's.
    LocalDataSource(std::vector<std::string> roots, std::vector<std::string> cameraRolls)
        : mRoots(std::move(roots)), mCameraRolls(std::move(cameraRolls)) {}

    void loadMediaSets(MediaFeed *feed) override;
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override;
    bool performOperation(int operation, MediaItem *item, const void *data) override;
    bool supportsOperation(int operation) const override;

    bool supportsRegions(const MediaItem *item) const override;
    void requestRegion(MediaItem *item, int x, int y, int width, int height, int outWidth, int outHeight,
                       RegionCallback done) override;
    bool readThumbnail(MediaItem *item, int maxEdge, Bitmap *bitmap) override;

    static bool isSupportedImage(const std::string &path);
    static std::string mimeTypeForPath(const std::string &path);

    // The files without each camera RAW that has a JPEG or HEIF of the same
    // name beside it. A camera set to save both writes the pair, and the pair
    // is one photo. The order is kept.
    static std::vector<std::string> withoutRawDuplicates(std::vector<std::string> files);

    // Where a scan gets what the platform already knows: the pictures under
    // the folders it covers, and what it has read about each picture.
    // PhotoIndex::query unless replaced, which a test does to stand in for the
    // platform's index.
    using IndexLookup = std::function<PhotoIndex::Listing(const std::vector<std::string> &folders)>;
    void setIndexLookup(IndexLookup lookup) {
        mIndexLookup = std::move(lookup);
    }

  private:
    struct Folder {
        std::string path;
        std::string name;
        std::vector<std::string> files;
    };

    // Walks path and every folder under it.
    void scan(const std::string &path, std::vector<Folder> &folders) const;

    // The same folders scan would find under root, taken from the index's
    // entries instead of the disk, in the order scan finds them.
    void list(const std::string &root, const PhotoIndex::Entries &entries, std::vector<Folder> &folders) const;

    std::vector<std::string> mRoots;
    std::vector<std::string> mCameraRolls;
    IndexLookup mIndexLookup = PhotoIndex::query;
    RegionDecoderCache mDecoders;
};
