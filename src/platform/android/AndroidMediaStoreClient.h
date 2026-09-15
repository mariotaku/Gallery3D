// The media store as the Java bridge reads it.
#pragma once

#include "media/MediaStoreClient.h"

class AndroidMediaStoreClient : public MediaStoreClient {
  public:
    bool requestPermission() override;
    std::string queryBuckets() override;
    std::string queryBucket(const std::string &bucketId) override;
    bool readImage(int64_t id, std::vector<uint8_t> *bytes) override;
};
