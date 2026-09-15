// A media store the tests fill. It answers MediaStoreDataSource the way
// MediaStoreBridge.java does: folders in the order of their newest photo,
// photos newest first, as JSON, and bytes read from a file on this disk.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "media/MediaStoreClient.h"

struct FakePhoto {
    int64_t id = 0;
    std::string bucketId;
    std::string bucketName;
    bool camera = false;
    std::string name;
    std::string mime;
    int64_t dateTaken = 0;
    int64_t dateModified = 0;
    int64_t dateAdded = 0;
    int orientation = 0;
    int width = 0;
    int height = 0;
    // The file readImage hands back. Empty for a photo that cannot be read.
    std::string file;
};

class FakeMediaStore : public MediaStoreClient {
  public:
    bool requestPermission() override;
    std::string queryBuckets() override;
    std::string queryBucket(const std::string &bucketId) override;
    bool readImage(int64_t id, std::vector<uint8_t> *bytes) override;

    std::vector<FakePhoto> photos;
    // What the user answers the permission request with.
    bool permitted = true;

    std::atomic<int> permissionRequests{0};
    std::atomic<int> folderQueries{0};
    std::atomic<int> photoQueries{0};
};
