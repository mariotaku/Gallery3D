#include "platform/android/AndroidMediaStoreClient.h"

#include "platform/android/AndroidBridge.h"

bool AndroidMediaStoreClient::requestPermission() {
    return AndroidBridge::requestMediaPermission();
}

std::string AndroidMediaStoreClient::queryBuckets() {
    return AndroidBridge::queryBuckets();
}

std::string AndroidMediaStoreClient::queryBucket(const std::string &bucketId) {
    return AndroidBridge::queryBucket(bucketId);
}

bool AndroidMediaStoreClient::readImage(int64_t id, std::vector<uint8_t> *bytes) {
    return AndroidBridge::readImage(id, bytes);
}

bool AndroidMediaStoreClient::readThumbnail(int64_t id, int maxEdge, Bitmap *bitmap, bool *upright) {
    return AndroidBridge::readMediaThumbnail(id, maxEdge, bitmap, upright);
}
