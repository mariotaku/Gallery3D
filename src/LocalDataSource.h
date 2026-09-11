// Port of com.cooliris.media.DataSource and LocalDataSource.
// Walks a directory tree: one MediaSet per image folder, one MediaItem per image.
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

    // Whether item reads use the network; routes loads to a separate pool from local decoding.
    virtual bool readsBlockOnNetwork() const {
        return false;
    }

    // Called with the item's encoded bytes, or with false. May answer before it
    // returns or long after, so the caller has to be written for both.
    using BytesCallback = std::function<void(bool ok, std::vector<uint8_t> bytes)>;

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

    // Whether the source can supply cropped regions for tiled fullscreen rendering.
    // SDL_image and web stb decode whole files only; IIIF performs cropping on the server.
    virtual bool supportsRegions() const {
        return false;
    }

    // Requests an encoded region in original-image pixels, scaled to outWidth by outHeight.
    // Only used with supportsRegions; callback may run inline or later.
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
