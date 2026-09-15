// MediaStoreDataSource on every platform, against a fake media store that
// answers the way the Java bridge does, and against LocalDataSource over the
// same library. The rules a source keeps whichever store is behind it:
// - loadMediaSets finishes, whether or not the user allows the library.
// - A set's id is stable and not negative.
// - A RAW beside a JPEG or HEIF of the same name is left out.
// - Items carry what the store says about them, and bytes come back whole.
// - A request for a region or a page is answered once, even when it cannot be.
#include "tests.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "decode_fixtures.h"
#include "fake_media_store.h"
#include "graphics/Bitmap.h"
#include "media/ConcatenatedDataSource.h"
#include "media/LocalDataSource.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "media/MediaStoreDataSource.h"

namespace fs = std::filesystem;

namespace {

// Waits up to five seconds for something the loader thread does.
bool eventually(const std::function<bool()> &condition) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

FakePhoto photo(int64_t id, const std::string &bucketId, const std::string &bucketName, const std::string &name,
                const std::string &mime, int64_t dateTaken) {
    FakePhoto made;
    made.id = id;
    made.bucketId = bucketId;
    made.bucketName = bucketName;
    made.name = name;
    made.mime = mime;
    made.dateTaken = dateTaken;
    made.dateModified = dateTaken / 1000;
    made.dateAdded = dateTaken / 1000;
    return made;
}

// Two folders: the camera's, and one holding a RAW beside its JPEG.
void fillLibrary(FakeMediaStore &store) {
    FakePhoto first = photo(11, "-1739773001", "Camera", "IMG_1.jpg", "image/jpeg", 1757937600000LL);
    first.camera = true;
    first.width = 320;
    first.height = 240;
    first.orientation = 90;
    first.file = DecodeFixtures::folder() + "orientation_1.jpg";
    FakePhoto second = photo(12, "-1739773001", "Camera", "IMG_2.jpg", "image/jpeg", 1757937500000LL);
    second.camera = true;
    store.photos = {first, second, photo(21, "5550123", "Holiday", "beach.png", "image/png", 1700000000000LL),
                    photo(22, "5550123", "Holiday", "DSC_3.ARW", "image/x-sony-arw", 1700000100000LL),
                    photo(23, "5550123", "Holiday", "DSC_3.JPG", "image/jpeg", 1700000100000LL)};
    // A photo the store has no date for, which falls back to its modified time.
    FakePhoto undated = photo(24, "5550123", "Holiday", "scan.png", "image/png", 0);
    undated.dateModified = 1600000000;
    store.photos.push_back(undated);
}

struct Album {
    std::string name;
    bool camera = false;
    std::vector<std::string> captions;
};

// What the wall would show, in an order that does not depend on the source.
std::vector<Album> albumsOf(MediaFeed &feed) {
    feed.pumpListener();
    std::vector<Album> albums;
    for (MediaSet *set : feed.getMediaSets()) {
        Album album;
        album.name = set->mName;
        album.camera = set->mIsCameraRoll;
        for (int i = 0; i < set->getNumItems(); ++i) {
            album.captions.push_back(set->getItems()[(size_t)i]->mCaption);
        }
        std::sort(album.captions.begin(), album.captions.end());
        albums.push_back(album);
    }
    std::sort(albums.begin(), albums.end(), [](const Album &a, const Album &b) { return a.name < b.name; });
    return albums;
}

MediaSet *setNamed(MediaFeed &feed, const std::string &name) {
    for (MediaSet *set : feed.getMediaSets()) {
        if (set->mName == name) {
            return set;
        }
    }
    return nullptr;
}

MediaItem *itemCaptioned(MediaSet *set, const std::string &caption) {
    for (int i = 0; set != nullptr && i < set->getNumItems(); ++i) {
        if (set->getItems()[(size_t)i]->mCaption == caption) {
            return set->getItems()[(size_t)i];
        }
    }
    return nullptr;
}

}  // namespace

TEST(a_refused_library_queries_nothing_and_still_finishes_loading) {
    FakeMediaStore store;
    fillLibrary(store);
    store.permitted = false;
    MediaStoreDataSource source(store);
    MediaFeed feed(&source, nullptr);
    feed.start();
    CHECK(eventually([&feed]() { return !feed.isLoading(); }));
    CHECK_EQ(store.permissionRequests.load(), 1);
    CHECK_EQ(store.folderQueries.load(), 0);
    CHECK(albumsOf(feed).empty());
}

TEST(media_store_sets_have_stable_ids_that_are_not_negative) {
    FakeMediaStore store;
    fillLibrary(store);
    std::vector<int64_t> ids[2];
    for (int round = 0; round < 2; ++round) {
        MediaStoreDataSource source(store);
        MediaFeed feed(&source, nullptr);
        source.loadMediaSets(&feed);
        feed.pumpListener();
        for (MediaSet *set : feed.getMediaSets()) {
            CHECK_DETAIL(set->mId >= 0, set->mName + " has a negative id");
            ids[round].push_back(set->mId);
        }
    }
    CHECK_EQ(ids[0].size(), (size_t)2);
    CHECK(ids[0] == ids[1]);
}

TEST(media_store_items_carry_what_the_store_says) {
    FakeMediaStore store;
    fillLibrary(store);
    MediaStoreDataSource source(store);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();

    MediaSet *camera = setNamed(feed, "Camera");
    CHECK(camera != nullptr);
    if (camera != nullptr) {
        CHECK(camera->mIsCameraRoll);
        MediaItem *first = itemCaptioned(camera, "IMG_1.jpg");
        CHECK(first != nullptr);
        if (first != nullptr) {
            CHECK_EQ(first->mId, (int64_t)11);
            CHECK(first->mMimeType == "image/jpeg");
            CHECK_EQ(first->mRotation, 90.0f);
            CHECK_EQ(first->mFullWidth, 320);
            CHECK_EQ(first->mFullHeight, 240);
            CHECK_EQ(first->mDateTakenInMs, (int64_t)1757937600000LL);
            // No path, but a uri for every size, or the fullscreen view would
            // never ask for a screennail.
            CHECK(first->mFilePath.empty());
            CHECK(!first->mContentUri.empty());
            CHECK(first->mScreennailUri == first->mContentUri);
        }
    }

    MediaSet *holiday = setNamed(feed, "Holiday");
    CHECK(holiday != nullptr);
    if (holiday != nullptr) {
        CHECK(!holiday->mIsCameraRoll);
        // The RAW beside its JPEG is left out, as the desktop leaves it out.
        CHECK(itemCaptioned(holiday, "DSC_3.ARW") == nullptr);
        CHECK(itemCaptioned(holiday, "DSC_3.JPG") != nullptr);
        MediaItem *undated = itemCaptioned(holiday, "scan.png");
        CHECK(undated != nullptr);
        if (undated != nullptr) {
            CHECK_EQ(undated->mDateTakenInMs, (int64_t)1600000000000LL);
        }
    }
}

TEST(media_store_bytes_come_back_whole) {
    FakeMediaStore store;
    fillLibrary(store);
    MediaStoreDataSource source(store);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();
    MediaItem *first = itemCaptioned(setNamed(feed, "Camera"), "IMG_1.jpg");
    CHECK(first != nullptr);
    if (first == nullptr) {
        return;
    }
    std::vector<uint8_t> expected;
    std::vector<uint8_t> got;
    CHECK(Bitmap::readFile(DecodeFixtures::folder() + "orientation_1.jpg", &expected));
    CHECK(source.readItemBytes(first, &got));
    CHECK(got == expected);
    // One the store cannot read hands back nothing.
    MediaItem *unreadable = itemCaptioned(setNamed(feed, "Camera"), "IMG_2.jpg");
    CHECK(unreadable != nullptr && !source.readItemBytes(unreadable, &got));
}

TEST(a_region_is_answered_once_even_when_it_cannot_be) {
    FakeMediaStore store;
    fillLibrary(store);
    MediaStoreDataSource media(store);
    LocalDataSource local(fs::temp_directory_path().string());
    MediaItem sizeless;
    sizeless.mContentUri = "content://media/external/images/media/99";
    sizeless.mFilePath = (fs::temp_directory_path() / "gallery3d_missing_region.jpg").string();
    sizeless.mMimeType = "image/jpeg";
    MediaItem missing;
    missing.mContentUri = sizeless.mContentUri;
    missing.mFilePath = sizeless.mFilePath;
    missing.mMimeType = sizeless.mMimeType;
    missing.mFullWidth = 640;
    missing.mFullHeight = 480;
    for (DataSource *source : {(DataSource *)&media, (DataSource *)&local}) {
        for (MediaItem *item : {&sizeless, &missing}) {
            int answers = 0;
            bool valid = false;
            source->requestRegion(item, 0, 0, 64, 64, 1, [&answers, &valid](Bitmap tile) {
                ++answers;
                valid = tile.valid();
            });
            CHECK_EQ(answers, 1);
            CHECK(!valid);
        }
    }
}

TEST(a_set_no_source_owns_is_still_finished) {
    // A set routed to the concatenated source itself has no owner to load it.
    // The page must still finish, or the set stays in flight for good.
    FakeMediaStore store;
    fillLibrary(store);
    MediaStoreDataSource media(store);
    // An empty folder, so the walk after the media store's is over at once and
    // the page request below is not queued behind it.
    const fs::path empty = fs::temp_directory_path() / "gallery3d_empty_library";
    std::error_code error;
    fs::remove_all(empty, error);
    fs::create_directories(empty, error);
    LocalDataSource local(empty.string());
    ConcatenatedDataSource both(&media, &local);
    MediaFeed feed(&both, nullptr);
    feed.start();
    CHECK(eventually([&feed]() { return !feed.isLoading(); }));
    auto orphan = std::make_unique<MediaSet>();
    orphan->mId = 4242;
    orphan->mDataSource = &both;
    feed.addMediaSet(std::move(orphan));
    feed.pumpListener();
    MediaSet *set = nullptr;
    for (MediaSet *candidate : feed.getMediaSets()) {
        if (candidate->mId == 4242) {
            set = candidate;
        }
    }
    CHECK(set != nullptr);
    if (set == nullptr) {
        return;
    }
    feed.loadItemsForSet(set);
    CHECK(eventually([&feed, set]() { return !feed.isLoadingItemsForSet(set); }));
}

namespace {

// Records whether the feed is still loading when its own enumeration starts.
class LoadingWitness : public DataSource {
  public:
    // -1 before loadMediaSets runs, then 1 when the feed was loading, 0 when not.
    std::atomic<int> sawLoading{-1};

    void loadMediaSets(MediaFeed *feed) override {
        sawLoading.store(feed->isLoading() ? 1 : 0);
        feed->finishLoadingMediaSets();
    }
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override {
        feed->finishLoadingItemsForSet(parentSet);
    }
};

}  // namespace

TEST(a_concatenated_feed_loads_until_its_second_source_finishes) {
    // The media store finishes its own part first. The feed must not report
    // done while the second source still adds sets.
    FakeMediaStore store;
    fillLibrary(store);
    MediaStoreDataSource media(store);
    LoadingWitness second;
    ConcatenatedDataSource both(&media, &second);
    MediaFeed feed(&both, nullptr);
    feed.start();
    CHECK(eventually([&second]() { return second.sawLoading.load() != -1; }));
    CHECK_EQ(second.sawLoading.load(), 1);
    CHECK(eventually([&feed]() { return !feed.isLoading(); }));
}

TEST(the_local_and_media_store_sources_show_one_library_alike) {
    const fs::path root = fs::temp_directory_path() / "gallery3d_media_store_library";
    std::error_code error;
    fs::remove_all(root, error);
    for (const char *file :
         {"Camera/IMG_1.jpg", "Camera/IMG_2.jpg", "Holiday/beach.png", "Holiday/DSC_3.ARW", "Holiday/DSC_3.JPG",
          "Holiday/scan.png"}) {
        // An empty file with a photo's extension, which is all a scan reads.
        fs::create_directories((root / file).parent_path(), error);
        std::FILE *handle = std::fopen((root / file).string().c_str(), "wb");
        if (handle != nullptr) {
            std::fclose(handle);
        }
    }

    LocalDataSource local({root.string()}, {(root / "Camera").string()});
    MediaFeed localFeed(&local, nullptr);
    local.loadMediaSets(&localFeed);

    FakeMediaStore store;
    fillLibrary(store);
    MediaStoreDataSource media(store);
    MediaFeed mediaFeed(&media, nullptr);
    media.loadMediaSets(&mediaFeed);

    const std::vector<Album> walked = albumsOf(localFeed);
    const std::vector<Album> listed = albumsOf(mediaFeed);
    CHECK_EQ(walked.size(), listed.size());
    for (size_t i = 0; i < std::min(walked.size(), listed.size()); ++i) {
        CHECK_DETAIL(walked[i].name == listed[i].name, walked[i].name + " against " + listed[i].name);
        CHECK_DETAIL(walked[i].camera == listed[i].camera, walked[i].name + ": camera roll differs");
        CHECK_DETAIL(walked[i].captions == listed[i].captions, walked[i].name + ": photos differ");
    }
    fs::remove_all(root, error);
}
