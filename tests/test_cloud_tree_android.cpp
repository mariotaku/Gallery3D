// The folder tree walk against a documents provider shaped like a large cloud
// drive: FakeCloudProvider in the conformance build, read through the real
// StorageBridge, AndroidDocumentTreeClient and DocumentTreeDataSource. 1111
// folder listings, 1000 albums of 20 photos, a network's round trip on every
// query, and some folders that answer with part of their photos marked
// EXTRA_LOADING before the rest. A subtree is also walked on each network the
// provider plays, and its thumbnails read. Android only.
#include "tests.h"

#include <SDL3/SDL.h>

#include <malloc.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "media/DocumentTreeDataSource.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "platform/android/AndroidDocumentTreeClient.h"

namespace {

const char *const kCloud = "content://me.mariotaku.gallery3d.conformance.fakecloud/tree/";
// The whole drive on the fast home network.
const std::string kCloudTree = std::string(kCloud) + "home%40root";
const int kFolders = 1111;
const int kAlbums = 1000;
const int kPhotosPerAlbum = 20;

struct Network {
    const char *id;
    const char *title;
    uint64_t roundTripMs;
};
const Network kNetworks[] = {
    {"home", "Fast home network", 5}, {"5g", "5G", 20}, {"4g", "4G", 60}, {"bad4g", "Bad 4G", 400}};

// Ten albums under root/A0/B0, 11 folder listings, on one network.
std::string subtreeOn(const char *network) {
    return std::string(kCloud) + network + "%40root%2FA0%2FB0";
}

MediaItem *photoNamed(MediaSet *set, const std::string &name) {
    for (int i = 0; set != nullptr && i < set->getNumItems(); ++i) {
        if (set->getItems()[(size_t)i]->mCaption == name) {
            return set->getItems()[(size_t)i];
        }
    }
    return nullptr;
}

// A field of /proc/self/status in kilobytes: VmRSS is the memory this process
// holds now, VmHWM the most it held since resetPeakMemory.
long statusKilobytes(const char *field) {
    std::FILE *status = std::fopen("/proc/self/status", "r");
    if (status == nullptr) {
        return -1;
    }
    long value = -1;
    char line[256];
    const size_t length = std::strlen(field);
    while (std::fgets(line, sizeof(line), status) != nullptr) {
        if (std::strncmp(line, field, length) == 0 && line[length] == ':') {
            value = std::strtol(line + length + 1, nullptr, 10);
            break;
        }
    }
    std::fclose(status);
    return value;
}

// Starts VmHWM again from what the process holds now.
void resetPeakMemory() {
    if (std::FILE *refs = std::fopen("/proc/self/clear_refs", "w")) {
        std::fputs("5", refs);
        std::fclose(refs);
    }
}

// Bytes malloc has handed out and not had back.
long nativeHeapBytes() {
    return (long)mallinfo().uordblks;
}

// Passes every call to the bridge, counting folder listings and noting when
// the first photos came back.
class TimedClient : public DocumentTreeClient {
  public:
    std::string listFolder(const std::string &folderUri) override {
        ++folders;
        if (onFolder) {
            onFolder(folders);
        }
        std::string answer = mBridge.listFolder(folderUri);
        if (firstPhotosMs == 0 && answer.find("\"photos\":[{") != std::string::npos) {
            firstPhotosMs = SDL_GetTicks() - started;
        }
        return answer;
    }
    std::string readExif(const std::string &uri, const std::string &mime) override {
        ++exifReads;
        return mBridge.readExif(uri, mime);
    }
    bool readThumbnail(const std::string &uri, int maxEdge, Bitmap *bitmap, int *orientation) override {
        ++thumbnailReads;
        return mBridge.readThumbnail(uri, maxEdge, bitmap, orientation);
    }
    bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) override {
        return mBridge.readDocument(uri, bytes);
    }

    const uint64_t started = SDL_GetTicks();
    uint64_t firstPhotosMs = 0;
    int folders = 0;
    int exifReads = 0;
    int thumbnailReads = 0;
    std::function<void(int folders)> onFolder;

  private:
    AndroidDocumentTreeClient mBridge;
};

}  // namespace

TEST(a_large_cloud_tree_walks_to_every_album_with_all_its_photos) {
    resetPeakMemory();
    const long residentBefore = statusKilobytes("VmRSS");
    const long heapBefore = nativeHeapBytes();

    TimedClient client;
    DocumentTreeDataSource source(client, kCloudTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    const uint64_t elapsed = SDL_GetTicks() - client.started;
    feed.pumpListener();

    // With every album still held by the feed, as the wall holds them.
    const long residentAfter = statusKilobytes("VmRSS");
    const long residentPeak = statusKilobytes("VmHWM");
    const long heapGrowth = nativeHeapBytes() - heapBefore;
    reportNote("memory: resident " + std::to_string(residentBefore / 1024) + " MB before, " +
               std::to_string(residentAfter / 1024) + " MB after, " + std::to_string(residentPeak / 1024) +
               " MB at the peak; native heap +" + std::to_string(heapGrowth / (1024 * 1024)) + " MB, " +
               std::to_string(heapGrowth / (kAlbums * kPhotosPerAlbum)) + " bytes a photo");
    // About 520 bytes a photo on the emulator. Four times that is a leak or a
    // copy per photo, not noise.
    CHECK_DETAIL(heapGrowth < (long)kAlbums * kPhotosPerAlbum * 2048,
                 "the walk grew the native heap by " + std::to_string(heapGrowth) + " bytes");
    const std::vector<MediaSet *> sets = feed.getMediaSets();
    if (sets.empty() && client.folders <= 1) {
        SKIP("no fake cloud provider in this build");
    }

    CHECK_EQ(client.folders, kFolders);
    CHECK_EQ((int)sets.size(), kAlbums);
    int photos = 0;
    int partial = 0;
    for (MediaSet *set : sets) {
        photos += set->getNumItems();
        if (set->getNumItems() != kPhotosPerAlbum) {
            ++partial;
        }
    }
    CHECK_EQ(photos, kAlbums * kPhotosPerAlbum);
    // A folder still loading in the provider is waited for, not taken half.
    CHECK_DETAIL(partial == 0, std::to_string(partial) + " albums came back with part of their photos");
    // The first album is on the wall long before the last folder is listed.
    CHECK_DETAIL(client.firstPhotosMs * 4 < elapsed, "first photos after " + std::to_string(client.firstPhotosMs) +
                                                         " ms of " + std::to_string(elapsed) + " ms");
    reportNote("walked " + std::to_string(client.folders) + " folders in " + std::to_string(elapsed) +
               " ms, the first photos after " + std::to_string(client.firstPhotosMs) + " ms");
}

TEST(a_large_cloud_walk_stops_soon_after_the_feed_shuts_down) {
    TimedClient client;
    DocumentTreeDataSource source(client, kCloudTree);
    MediaFeed feed(&source, nullptr);
    // As a switch to another source does part way through the walk.
    client.onFolder = [&feed](int folders) {
        if (folders == 150) {
            feed.shutdown();
        }
    };
    source.loadMediaSets(&feed);
    if (client.folders <= 1) {
        SKIP("no fake cloud provider in this build");
    }
    CHECK_EQ(client.folders, 150);
    feed.pumpListener();
    CHECK((int)feed.getMediaSets().size() < kAlbums);
}

TEST(a_cloud_subtree_walks_and_shows_thumbnails_on_every_network) {
    for (const Network &network : kNetworks) {
        TimedClient client;
        DocumentTreeDataSource source(client, subtreeOn(network.id));
        MediaFeed feed(&source, nullptr);
        source.loadMediaSets(&feed);
        const uint64_t walkMs = SDL_GetTicks() - client.started;
        feed.pumpListener();
        const std::vector<MediaSet *> sets = feed.getMediaSets();
        if (sets.empty() && client.folders <= 1) {
            SKIP("no fake cloud provider in this build");
        }
        CHECK_EQ(client.folders, 11);
        CHECK_EQ((int)sets.size(), 10);
        // Each listing is a round trip at the least.
        CHECK_DETAIL(walkMs >= 11 * network.roundTripMs,
                     std::string(network.title) + " walked in " + std::to_string(walkMs) + " ms");

        MediaSet *album = sets.empty() ? nullptr : sets.front();
        MediaItem *rotated = photoNamed(album, "IMG_0.jpg");
        MediaItem *upright = photoNamed(album, "IMG_1.jpg");
        MediaItem *plain = photoNamed(album, "IMG_2.jpg");
        CHECK(rotated != nullptr && upright != nullptr && plain != nullptr);
        if (rotated == nullptr || upright == nullptr || plain == nullptr) {
            continue;
        }

        const uint64_t thumbnailStarted = SDL_GetTicks();
        Bitmap thumbnail;
        CHECK(source.readThumbnail(rotated, 256, &thumbnail));
        const uint64_t thumbnailMs = SDL_GetTicks() - thumbnailStarted;
        // Stored landscape. The provider's rotation stands, and the photo,
        // which a cloud provider downloads to open, stays closed.
        CHECK(thumbnail.width() > thumbnail.height());
        CHECK_EQ(client.exifReads, 0);
        CHECK(rotated->takeLateDetails());
        CHECK_EQ(rotated->mRotation, 90.0f);

        // No rotation reported, so the EXIF is asked for. This provider opens
        // no photo, which leaves the thumbnail as it came.
        Bitmap uprightThumbnail;
        CHECK(source.readThumbnail(upright, 256, &uprightThumbnail));
        CHECK_EQ(client.exifReads, 1);
        CHECK(uprightThumbnail.width() > uprightThumbnail.height());

        // A photo listed without thumbnails asks the provider for none.
        const int thumbnailReads = client.thumbnailReads;
        Bitmap none;
        CHECK(!source.readThumbnail(plain, 256, &none));
        CHECK_EQ(client.thumbnailReads, thumbnailReads);

        reportNote(std::string(network.title) + ": 11 folders in " + std::to_string(walkMs) +
                   " ms, a thumbnail in " + std::to_string(thumbnailMs) + " ms");
    }
}

TEST(an_offline_cloud_finishes_with_no_albums_and_no_thumbnails) {
    TimedClient client;
    DocumentTreeDataSource source(client, subtreeOn("offline"));
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();
    CHECK(feed.getMediaSets().empty());
    CHECK_EQ(client.folders, 1);

    const std::string photo = subtreeOn("offline") + "/document/offline%40root%2FA0%2FB0%2FC0%2FIMG_0.jpg";
    Bitmap thumbnail;
    int orientation = 0;
    CHECK(!client.readThumbnail(photo, 256, &thumbnail, &orientation));
    CHECK_EQ(orientation, -1);
}
