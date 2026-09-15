// DocumentTreeDataSource on every platform, against a fake folder tree that
// answers the way the Java bridge does. The rules it keeps:
// - One album per folder that holds photos, named as the folder is.
// - A RAW beside a JPEG or HEIF of the same name is left out.
// - Set and item ids are stable and not negative.
// - Items carry what the tree says about them, and bytes come back whole.
// - A tree that cannot be read still finishes loading, with no albums.
#include "tests.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <vector>

#include "decode_fixtures.h"
#include "fake_document_tree.h"
#include "graphics/Bitmap.h"
#include "media/DocumentTreeDataSource.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"

namespace {

const char *const kTree = "content://com.android.externalstorage.documents/tree/1234-5678%3APictures";
const char *const kPictures =
    "content://com.android.externalstorage.documents/tree/1234-5678%3APictures/document/1234-5678%3APictures";
const char *const kTrip =
    "content://com.android.externalstorage.documents/tree/1234-5678%3APictures/document/1234-5678%3APictures%2FTrip";

FakeDocument document(const std::string &folderUri, const std::string &folderName, const std::string &name,
                      const std::string &mime, int64_t dateTaken) {
    FakeDocument made;
    made.folderUri = folderUri;
    made.folderName = folderName;
    made.uri = folderUri + "%2F" + name;
    made.name = name;
    made.mime = mime;
    made.dateTaken = dateTaken;
    made.dateModified = dateTaken;
    return made;
}

// The tree's own folder with one photo, and a subfolder holding a RAW beside
// its JPEG and a photo with no date of its own.
void fillTree(FakeDocumentTree &tree) {
    tree.treeUri = kTree;
    FakeDocument first = document(kPictures, "Pictures", "IMG_1.jpg", "image/jpeg", 1757937600000LL);
    first.orientation = 90;
    first.width = 320;
    first.height = 240;
    first.file = DecodeFixtures::folder() + "orientation_1.jpg";
    FakeDocument undated = document(kTrip, "Trip", "scan.png", "image/png", 0);
    undated.dateModified = 1600000000000LL;
    tree.documents = {first, document(kTrip, "Trip", "DSC_3.ARW", "image/x-sony-arw", 1700000100000LL),
                      document(kTrip, "Trip", "DSC_3.JPG", "image/jpeg", 1700000100000LL), undated};
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

std::vector<std::string> captionsOf(MediaSet *set) {
    std::vector<std::string> captions;
    for (int i = 0; set != nullptr && i < set->getNumItems(); ++i) {
        captions.push_back(set->getItems()[(size_t)i]->mCaption);
    }
    std::sort(captions.begin(), captions.end());
    return captions;
}

}  // namespace

TEST(a_document_tree_shows_one_album_per_folder_and_hides_raw_beside_jpeg) {
    FakeDocumentTree tree;
    fillTree(tree);
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();

    CHECK_EQ((int)feed.getMediaSets().size(), 2);
    CHECK(captionsOf(setNamed(feed, "Pictures")) == std::vector<std::string>{"IMG_1.jpg"});
    CHECK(captionsOf(setNamed(feed, "Trip")) == (std::vector<std::string>{"DSC_3.JPG", "scan.png"}));
    CHECK(!feed.isLoading());
}

TEST(document_tree_ids_are_stable_and_not_negative) {
    std::vector<int64_t> ids[2];
    for (std::vector<int64_t> &run : ids) {
        FakeDocumentTree tree;
        fillTree(tree);
        DocumentTreeDataSource source(tree, kTree);
        MediaFeed feed(&source, nullptr);
        source.loadMediaSets(&feed);
        feed.pumpListener();
        for (MediaSet *set : feed.getMediaSets()) {
            run.push_back(set->mId);
            for (int i = 0; i < set->getNumItems(); ++i) {
                run.push_back(set->getItems()[(size_t)i]->mId);
            }
        }
    }
    CHECK(!ids[0].empty());
    CHECK(ids[0] == ids[1]);
    for (int64_t id : ids[0]) {
        CHECK(id >= 0);
    }
}

TEST(document_tree_items_carry_what_the_tree_says) {
    FakeDocumentTree tree;
    fillTree(tree);
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();

    MediaItem *photo = itemCaptioned(setNamed(feed, "Pictures"), "IMG_1.jpg");
    CHECK(photo != nullptr);
    if (photo != nullptr) {
        CHECK(photo->mContentUri == std::string(kPictures) + "%2FIMG_1.jpg");
        CHECK(photo->mScreennailUri == photo->mContentUri);
        CHECK(photo->mFilePath.empty());
        CHECK(photo->mMimeType == "image/jpeg");
        CHECK_EQ(photo->mRotation, 90.0f);
        CHECK_EQ(photo->mFullWidth, 320);
        CHECK_EQ(photo->mDateTakenInMs, 1757937600000LL);
    }
    // No date of its own, so the modified time stands in.
    MediaItem *undated = itemCaptioned(setNamed(feed, "Trip"), "scan.png");
    CHECK(undated != nullptr);
    if (undated != nullptr) {
        CHECK_EQ(undated->mDateTakenInMs, 1600000000000LL);
        CHECK_EQ(undated->mDateModifiedInSec, 1600000000LL);
    }
}

TEST(document_tree_bytes_come_from_the_document) {
    FakeDocumentTree tree;
    fillTree(tree);
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();

    MediaItem *photo = itemCaptioned(setNamed(feed, "Pictures"), "IMG_1.jpg");
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> expected;
    CHECK(Bitmap::readFile(DecodeFixtures::folder() + "orientation_1.jpg", &expected));
    CHECK(source.readItemBytes(photo, &bytes));
    CHECK(bytes == expected);

    // A document with no file behind it, as one deleted after the listing.
    MediaItem *gone = itemCaptioned(setNamed(feed, "Trip"), "scan.png");
    std::vector<uint8_t> none;
    CHECK(!source.readItemBytes(gone, &none));
}

TEST(a_document_tree_that_cannot_be_read_finishes_with_no_albums) {
    FakeDocumentTree tree;
    fillTree(tree);
    // A tree whose permission is gone answers nothing.
    DocumentTreeDataSource source(tree, "content://com.android.externalstorage.documents/tree/revoked");
    MediaFeed feed(&source, nullptr);
    feed.start();
    for (int attempt = 0; attempt < 500 && feed.isLoading(); ++attempt) {
        SDL_Delay(10);
    }
    CHECK(!feed.isLoading());
    feed.pumpListener();
    CHECK(feed.getMediaSets().empty());
    CHECK_EQ(tree.photoQueries.load(), 0);
}
