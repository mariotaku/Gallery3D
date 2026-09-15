// A granted folder tree the tests fill. It answers DocumentTreeDataSource the
// way StorageBridge.java does: one folder at a time, its subfolders and photos
// as JSON, EXIF for one photo, and bytes read from a file on this disk.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "media/DocumentTreeClient.h"

struct FakeFolder {
    std::string uri;
    std::string name;
    // Empty for the tree's own folder.
    std::string parentUri;
};

struct FakeDocument {
    std::string folderUri;
    std::string uri;
    std::string name;
    std::string mime;
    int64_t dateTaken = 0;
    int64_t dateModified = 0;
    int orientation = 0;
    int width = 0;
    int height = 0;
    // The file readDocument hands back. Empty for a document that cannot be read.
    std::string file;
    // The thumbnail the provider hands out. Invalid when it makes none.
    Bitmap thumbnail;
    // The rotation the provider reports with the thumbnail, or -1 for none.
    int thumbnailOrientation = -1;
    // False for a cloud provider, whose EXIF answer is only what it keeps and
    // leaves orientation at 0.
    bool exifFromFile = true;
};

class FakeDocumentTree : public DocumentTreeClient {
  public:
    std::string listFolder(const std::string &folderUri) override;
    std::string readExif(const std::string &uri, const std::string &mime) override;
    bool readThumbnail(const std::string &uri, int maxEdge, Bitmap *bitmap, int *orientation) override;
    bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) override;

    // The tree it answers for. Any other tree reads as revoked.
    std::string treeUri;
    std::vector<FakeFolder> folders;
    std::vector<FakeDocument> documents;
    // Called with each folder asked for, before the answer.
    std::function<void(const std::string &folderUri)> onListFolder;
    // Called with each photo whose EXIF is asked for, before the answer.
    std::function<void(const std::string &uri)> onReadExif;
    // A folder or photo uri answered as unreadable the next time it is asked
    // for, and read normally after.
    std::string failOnce;

    std::atomic<int> folderQueries{0};
    std::atomic<int> exifReads{0};
    std::atomic<int> thumbnailReads{0};
    std::atomic<int> lastThumbnailEdge{0};
};
