// What DocumentTreeDataSource asks of a folder the user granted through
// Android's storage access framework: a tree uri from ACTION_OPEN_DOCUMENT_TREE.
// On Android that is the Java bridge; the tests stand a fake in front of it, so
// the source's rules run on every platform.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Bitmap;

class DocumentTreeClient {
  public:
    virtual ~DocumentTreeClient() = default;

    // JSON: one object for one folder of the tree, with folders (its
    // subfolders, each with id, the folder's document uri, and name) and photos
    // (each with uri, the photo's document uri, name, mime, dateModified in
    // milliseconds, and thumbnail, true when the provider makes thumbnails of
    // it). Hidden entries are left out. folderUri is a folder's
    // document uri, or the tree uri itself for the tree's own folder, whose
    // object also carries its name. Empty when the folder cannot be read, for
    // instance once the tree's permission is revoked. Opens no photo, so a
    // folder takes one query to the provider.
    virtual std::string listFolder(const std::string &folderUri) = 0;

    // JSON: one object with what the photo's EXIF says: orientation in degrees,
    // dateTaken in milliseconds (0 when it has none), width and height (0 when
    // unknown), and fromFile, false when the answer is only what the provider
    // keeps because opening the photo would download it. Empty when the photo
    // cannot be read. Asked for only when the photo is about to be shown.
    virtual std::string readExif(const std::string &uri, const std::string &mime) = 0;

    // The provider's thumbnail of a photo listed with thumbnail, near maxEdge
    // on its long edge, with its pixels as the provider hands them out.
    // orientation is the rotation in degrees the provider reports the
    // thumbnail needs to show upright, or -1 when it reports none, and then the
    // thumbnail is upright already. False when the provider gives none.
    virtual bool readThumbnail(const std::string &uri, int maxEdge, Bitmap *bitmap, int *orientation) = 0;

    // The encoded bytes of one document. False when it cannot be read.
    virtual bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) = 0;
};
