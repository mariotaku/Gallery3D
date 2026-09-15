// A granted folder tree the tests fill. It answers DocumentTreeDataSource the
// way StorageBridge.java does: folders that hold photos, the photos in one
// folder, as JSON, and bytes read from a file on this disk.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "media/DocumentTreeClient.h"

struct FakeDocument {
    std::string folderUri;
    std::string folderName;
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
    std::string listFolders(const std::string &treeUri) override;
    std::string listPhotos(const std::string &folderUri) override;
    std::string readExif(const std::string &uri, const std::string &mime) override;
    bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) override;

    // The tree it answers for. Any other tree reads as revoked.
    std::string treeUri;
    std::vector<FakeDocument> documents;

    std::atomic<int> photoQueries{0};
    std::atomic<int> exifReads{0};
};
