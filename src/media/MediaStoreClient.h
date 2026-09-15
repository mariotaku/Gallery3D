// What MediaStoreDataSource asks of Android's media store. On Android that is
// the Java bridge; the tests stand a fake in front of it, so the source's rules
// run on every platform.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class MediaStoreClient {
  public:
    virtual ~MediaStoreClient() = default;

    // Asks for the photo permission and waits for the answer. Called from a
    // loader thread. False when the user refuses, and then nothing is queried.
    virtual bool requestPermission() = 0;

    // JSON, as MediaStoreBridge.queryBuckets writes it: one object per folder
    // of photos, with id (text), name, count, dateTaken and camera.
    virtual std::string queryBuckets() = 0;

    // JSON, as MediaStoreBridge.queryBucket writes it: one object per photo in
    // the folder, newest first, with id, name, mime, dateTaken, dateModified,
    // dateAdded, orientation in degrees, width and height.
    virtual std::string queryBucket(const std::string &bucketId) = 0;

    // The encoded bytes of one photo. False when it cannot be read.
    virtual bool readImage(int64_t id, std::vector<uint8_t> *bytes) = 0;
};
