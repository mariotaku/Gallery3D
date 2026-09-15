// The folder tree walk against a documents provider shaped like a large cloud
// drive: FakeCloudProvider in the conformance build, read through the real
// StorageBridge, AndroidDocumentTreeClient and DocumentTreeDataSource. 1111
// folder listings, 1000 albums of 20 photos, a millisecond of latency on every
// query, and some folders that answer with part of their photos marked
// EXTRA_LOADING before the rest. Android only.
#include "tests.h"

#include <SDL3/SDL.h>

#include <malloc.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "media/DocumentTreeDataSource.h"
#include "media/MediaFeed.h"
#include "media/MediaSet.h"
#include "platform/android/AndroidDocumentTreeClient.h"

namespace {

const char *const kCloudTree = "content://me.mariotaku.gallery3d.conformance.fakecloud/tree/root";
const int kFolders = 1111;
const int kAlbums = 1000;
const int kPhotosPerAlbum = 20;

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
        return mBridge.readExif(uri, mime);
    }
    bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) override {
        return mBridge.readDocument(uri, bytes);
    }

    const uint64_t started = SDL_GetTicks();
    uint64_t firstPhotosMs = 0;
    int folders = 0;
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
