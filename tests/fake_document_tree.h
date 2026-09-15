// A granted folder tree the tests fill. It answers DocumentTreeDataSource the
// way StorageBridge.java does: one folder at a time, its subfolders and photos
// as JSON, EXIF for one photo, and bytes read from a file on this disk.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
};

class FakeDocumentTree : public DocumentTreeClient {
  public:
    std::string listFolder(const std::string &folderUri) override;
    std::string readExif(const std::string &uri, const std::string &mime) override;
    bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) override;

    // The tree it answers for. Any other tree reads as revoked.
    std::string treeUri;
    std::vector<FakeFolder> folders;
    std::vector<FakeDocument> documents;
    // Called with each folder asked for, before the answer.
    std::function<void(const std::string &folderUri)> onListFolder;

    std::atomic<int> folderQueries{0};
    std::atomic<int> exifReads{0};
};
