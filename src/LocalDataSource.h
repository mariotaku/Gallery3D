// Port of com.cooliris.media.DataSource and LocalDataSource.
//
// The original read the MediaStore through a ContentResolver and kept a disk
// cache of thumbnails. This one walks a directory tree instead: one MediaSet
// per folder, one MediaItem per image file.
#pragma once

#include <cstdint>
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
