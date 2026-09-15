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

    // JSON: one object for one folder of the tree, with folders (its
    // subfolders, each with id, the folder's document uri, and name) and photos
    // (each with uri, the photo's document uri, name, mime and dateModified in
    // milliseconds). Hidden entries are left out. folderUri is a folder's
    // document uri, or the tree uri itself for the tree's own folder, whose
    // object also carries its name. Empty when the folder cannot be read, for
    // instance once the tree's permission is revoked. Opens no photo, so a
    // folder takes one query to the provider.
    virtual std::string listFolder(const std::string &folderUri) = 0;

    // JSON: one object with what the photo's EXIF says: orientation in degrees,
    // dateTaken in milliseconds (0 when it has none), width and height (0 when
    // unknown). Empty when the photo cannot be read. Opens the photo, so it is
    // asked for only when the photo is about to be shown.
    virtual std::string readExif(const std::string &uri, const std::string &mime) = 0;

    // The encoded bytes of one document. False when it cannot be read.
    virtual bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) = 0;
};
