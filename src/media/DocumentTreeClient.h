// What DocumentTreeDataSource asks of a folder the user granted through
// Android's storage access framework: a tree uri from ACTION_OPEN_DOCUMENT_TREE.
// On Android that is the Java bridge; the tests stand a fake in front of it, so
// the source's rules run on every platform.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class DocumentTreeClient {
  public:
    virtual ~DocumentTreeClient() = default;

    // JSON: one object per folder in the tree that holds photos, the tree's
    // own folder included, with id (the folder's document uri), name and count.
    // Empty or invalid when the tree cannot be read, for instance once its
    // permission is revoked.
    virtual std::string listFolders(const std::string &treeUri) = 0;

    // JSON: one object per photo directly in the folder, with uri (the photo's
    // document uri), name, mime, dateTaken and dateModified in milliseconds
    // (dateTaken 0 when the photo has no date of its own), orientation in
    // degrees, width and height.
    virtual std::string listPhotos(const std::string &folderUri) = 0;

    // The encoded bytes of one document. False when it cannot be read.
    virtual bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) = 0;
};
