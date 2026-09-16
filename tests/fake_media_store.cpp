#include "fake_media_store.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "graphics/Bitmap.h"

namespace {

// The rows as the bridge's cursor walks them: newest date taken first.
std::vector<FakePhoto> newestFirst(std::vector<FakePhoto> photos) {
    std::stable_sort(photos.begin(), photos.end(),
                     [](const FakePhoto &a, const FakePhoto &b) { return a.dateTaken > b.dateTaken; });
    return photos;
}

}  // namespace

bool FakeMediaStore::requestPermission() {
    ++permissionRequests;
    return permitted;
}

std::string FakeMediaStore::queryBuckets() {
    ++folderQueries;
    nlohmann::json buckets = nlohmann::json::array();
    for (const FakePhoto &photo : newestFirst(photos)) {
        auto found = std::find_if(buckets.begin(), buckets.end(),
                                  [&photo](const nlohmann::json &bucket) { return bucket["id"] == photo.bucketId; });
        if (found == buckets.end()) {
            buckets.push_back({{"id", photo.bucketId},
                               {"name", photo.bucketName},
                               {"count", 1},
                               {"dateTaken", photo.dateTaken},
                               {"camera", photo.camera}});
        } else {
            (*found)["count"] = (*found)["count"].get<int>() + 1;
        }
    }
    return buckets.dump();
}

std::string FakeMediaStore::queryBucket(const std::string &bucketId) {
    ++photoQueries;
    nlohmann::json rows = nlohmann::json::array();
    for (const FakePhoto &photo : newestFirst(photos)) {
        if (photo.bucketId != bucketId) {
            continue;
        }
        rows.push_back({{"id", photo.id},
                        {"name", photo.name},
                        {"mime", photo.mime},
                        {"dateTaken", photo.dateTaken},
                        {"dateModified", photo.dateModified},
                        {"dateAdded", photo.dateAdded},
                        {"orientation", photo.orientation},
                        {"width", photo.width},
                        {"height", photo.height}});
    }
    return rows.dump();
}

bool FakeMediaStore::readThumbnail(int64_t id, int maxEdge, Bitmap *bitmap, bool *upright) {
    ++thumbnailReads;
    lastThumbnailEdge = maxEdge;
    if (bitmap == nullptr || upright == nullptr || maxEdge <= 0) {
        return false;
    }
    *upright = false;
    for (const FakePhoto &photo : photos) {
        if (photo.id == id && photo.thumbnail.valid()) {
            *bitmap = photo.thumbnail.cropped(0, 0, photo.thumbnail.width(), photo.thumbnail.height());
            *upright = photo.thumbnailUpright;
            return true;
        }
    }
    return false;
}

bool FakeMediaStore::readImage(int64_t id, std::vector<uint8_t> *bytes) {
    for (const FakePhoto &photo : photos) {
        if (photo.id == id) {
            return !photo.file.empty() && Bitmap::readFile(photo.file, bytes);
        }
    }
    return false;
}
