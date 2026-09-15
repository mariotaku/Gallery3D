// DocumentTreeDataSource on every platform, against a fake folder tree that
// answers the way the Java bridge does. The rules it keeps:
// - One album per folder that holds photos, named as the folder is, however
//   deep it is.
// - Each album reaches the feed as soon as its folder is listed.
// - The walk stops when the feed shuts down.
// - A RAW beside a JPEG or HEIF of the same name is left out.
// - Set and item ids are stable and not negative.
// - Items carry what the tree says about them, the EXIF only once prepared,
//   and bytes come back whole.
// - A provider thumbnail comes back as the photo is stored. The rotation the
//   provider reports with it is taken over the EXIF's, which is then not read
//   until the whole photo is shown.
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
const char *const kEmpty =
    "content://com.android.externalstorage.documents/tree/1234-5678%3APictures/document/1234-5678%3APictures%2FEmpty";
const char *const kDeep = "content://com.android.externalstorage.documents/tree/1234-5678%3APictures/document/"
                          "1234-5678%3APictures%2FEmpty%2FDeep";

FakeDocument document(const std::string &folderUri, const std::string &name, const std::string &mime,
                      int64_t dateTaken) {
    FakeDocument made;
    made.folderUri = folderUri;
    made.uri = folderUri + "%2F" + name;
    made.name = name;
    made.mime = mime;
    made.dateTaken = dateTaken;
    made.dateModified = dateTaken;
    return made;
}

// The tree's own folder with one photo; a subfolder holding a RAW beside its
// JPEG and a photo with no date of its own; and a folder with no photos whose
// own subfolder has one.
void fillTree(FakeDocumentTree &tree) {
    tree.treeUri = kTree;
    tree.folders = {{kPictures, "Pictures", ""}, {kTrip, "Trip", kPictures}, {kEmpty, "Empty", kPictures},
                    {kDeep, "Deep", kEmpty}};
    FakeDocument first = document(kPictures, "IMG_1.jpg", "image/jpeg", 1757937600000LL);
    first.orientation = 90;
    first.width = 320;
    first.height = 240;
    first.file = DecodeFixtures::folder() + "orientation_1.jpg";
    FakeDocument undated = document(kTrip, "scan.png", "image/png", 0);
    undated.dateModified = 1600000000000LL;
    tree.documents = {first, document(kTrip, "DSC_3.ARW", "image/x-sony-arw", 1700000100000LL),
                      document(kTrip, "DSC_3.JPG", "image/jpeg", 1700000100000LL), undated,
                      document(kDeep, "far.jpg", "image/jpeg", 1650000000000LL)};
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

    CHECK_EQ((int)feed.getMediaSets().size(), 3);
    CHECK(captionsOf(setNamed(feed, "Pictures")) == std::vector<std::string>{"IMG_1.jpg"});
    CHECK(captionsOf(setNamed(feed, "Trip")) == (std::vector<std::string>{"DSC_3.JPG", "scan.png"}));
    // Below a folder with no photos of its own.
    CHECK(captionsOf(setNamed(feed, "Deep")) == std::vector<std::string>{"far.jpg"});
    CHECK(setNamed(feed, "Empty") == nullptr);
    CHECK_EQ(tree.folderQueries.load(), 4);
    CHECK(!feed.isLoading());
}

TEST(a_document_tree_album_reaches_the_feed_before_the_walk_ends) {
    FakeDocumentTree tree;
    fillTree(tree);
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    bool shownWhileWalking = false;
    tree.onListFolder = [&feed, &shownWhileWalking](const std::string &folderUri) {
        if (folderUri == kDeep) {
            feed.pumpListener();
            shownWhileWalking = setNamed(feed, "Pictures") != nullptr && setNamed(feed, "Trip") != nullptr;
        }
    };
    source.loadMediaSets(&feed);
    CHECK(shownWhileWalking);
}

TEST(a_document_tree_walk_stops_when_the_feed_shuts_down) {
    FakeDocumentTree tree;
    fillTree(tree);
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    // As a switch to another source does part way through a large tree.
    tree.onListFolder = [&feed](const std::string &folderUri) {
        if (folderUri == kTrip) {
            feed.shutdown();
        }
    };
    source.loadMediaSets(&feed);
    // The tree's own folder and Trip. Empty and Deep are never asked for.
    CHECK_EQ(tree.folderQueries.load(), 2);
}

TEST(a_folder_listed_under_two_parents_is_walked_once) {
    FakeDocumentTree tree;
    fillTree(tree);
    // Trip again, under Empty, as a provider's shortcut would list it.
    tree.folders.push_back({kTrip, "Trip", kEmpty});
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();
    CHECK_EQ((int)feed.getMediaSets().size(), 3);
    CHECK_EQ(tree.folderQueries.load(), 4);
}

TEST(an_open_album_stays_open_while_more_albums_arrive) {
    MediaFeed feed(nullptr, nullptr);
    auto album = [](int64_t id, const std::string &name) {
        auto set = std::make_unique<MediaSet>();
        set->mId = id;
        set->mName = name;
        auto item = std::make_unique<MediaItem>();
        item->mId = id * 10;
        set->addItem(std::move(item));
        return set;
    };
    feed.addMediaSet(album(1, "First"));
    feed.addMediaSet(album(2, "Second"));
    feed.pumpListener();
    MediaSet *open = feed.getMediaSets()[1];
    feed.expandMediaSet(1);

    // Albums arriving later, one of them with the id of the open one, as a
    // folder listed twice would give.
    feed.addMediaSet(album(3, "Third"));
    feed.addMediaSet(album(2, "Second again"));
    feed.pumpListener();

    CHECK_EQ((int)feed.getMediaSets().size(), 3);
    CHECK(feed.getExpandedMediaSet() == open);
    CHECK(feed.getMediaSets()[1] == open);
    CHECK(open->mName == "Second");
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
        // The listing opens no photo, so what only the EXIF knows waits.
        CHECK_EQ(tree.exifReads.load(), 0);
        CHECK_EQ(photo->mRotation, 0.0f);

        source.prepareItem(photo, DataSource::ItemLoad::Thumbnail);
        source.prepareItem(photo, DataSource::ItemLoad::Whole);
        CHECK_EQ(tree.exifReads.load(), 1);
        CHECK(photo->takeLateDetails());
        CHECK(!photo->takeLateDetails());
        CHECK_EQ(photo->mRotation, 90.0f);
        CHECK_EQ(photo->mFullWidth, 320);
        CHECK_EQ(photo->mDateTakenInMs, 1757937600000LL);

        // The provider makes no thumbnails of it, so none is asked for.
        CHECK(photo->mThumbnailUri.empty());
        Bitmap thumbnail;
        CHECK(!source.readThumbnail(photo, 256, &thumbnail));
        CHECK_EQ(tree.thumbnailReads.load(), 0);
    }
    // No date of its own, so the modified time stands in.
    MediaItem *undated = itemCaptioned(setNamed(feed, "Trip"), "scan.png");
    CHECK(undated != nullptr);
    if (undated != nullptr) {
        CHECK_EQ(undated->mDateTakenInMs, 1600000000000LL);
        CHECK_EQ(undated->mDateModifiedInSec, 1600000000LL);
    }
}

TEST(a_provider_thumbnail_with_a_rotation_spares_the_exif) {
    FakeDocumentTree tree;
    fillTree(tree);
    // Stored 4 by 2. The EXIF says 90 degrees, the provider 270.
    tree.documents[0].thumbnail = Bitmap(4, 2);
    tree.documents[0].thumbnailOrientation = 270;
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();

    MediaItem *photo = itemCaptioned(setNamed(feed, "Pictures"), "IMG_1.jpg");
    CHECK(photo != nullptr);
    if (photo != nullptr) {
        CHECK(photo->mThumbnailUri == photo->mContentUri);
        source.prepareItem(photo, DataSource::ItemLoad::Thumbnail);
        Bitmap thumbnail;
        CHECK(source.readThumbnail(photo, 256, &thumbnail));
        CHECK_EQ(tree.lastThumbnailEdge.load(), 256);
        CHECK_EQ(thumbnail.width(), 4);
        CHECK_EQ(thumbnail.height(), 2);
        CHECK_EQ(tree.exifReads.load(), 0);
        CHECK(photo->takeLateDetails());
        CHECK_EQ(photo->mRotation, 270.0f);
        CHECK(!photo->hasFullSize());

        // The fullscreen view needs the size, which only the EXIF has.
        source.prepareItem(photo, DataSource::ItemLoad::Whole);
        CHECK_EQ(tree.exifReads.load(), 1);
        CHECK(photo->takeLateDetails());
        CHECK_EQ(photo->mRotation, 270.0f);
        CHECK_EQ(photo->mFullWidth, 320);
    }
}

TEST(an_upright_provider_thumbnail_is_turned_back_by_the_exif) {
    FakeDocumentTree tree;
    fillTree(tree);
    // Upright 2 by 4, with a red top left corner, and no rotation reported.
    Bitmap upright(2, 4);
    upright.pixels()[0] = 255;
    upright.pixels()[3] = 255;
    tree.documents[0].thumbnail = std::move(upright);
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();

    MediaItem *photo = itemCaptioned(setNamed(feed, "Pictures"), "IMG_1.jpg");
    CHECK(photo != nullptr);
    if (photo != nullptr) {
        source.prepareItem(photo, DataSource::ItemLoad::Thumbnail);
        CHECK_EQ(tree.exifReads.load(), 0);
        Bitmap thumbnail;
        CHECK(source.readThumbnail(photo, 256, &thumbnail));
        CHECK_EQ(tree.exifReads.load(), 1);
        CHECK_EQ(thumbnail.width(), 4);
        CHECK_EQ(thumbnail.height(), 2);
        // Stored so that a quarter turn clockwise puts the corner top left.
        if (thumbnail.width() == 4 && thumbnail.height() == 2) {
            CHECK_EQ((int)thumbnail.pixels()[4 * 4], 255);
        }
        CHECK(photo->takeLateDetails());
        CHECK_EQ(photo->mRotation, 90.0f);
    }
}

TEST(a_cached_thumbnail_learns_the_rotation_from_a_small_provider_thumbnail) {
    FakeDocumentTree tree;
    fillTree(tree);
    tree.documents[0].thumbnail = Bitmap(4, 2);
    tree.documents[0].thumbnailOrientation = 180;
    DocumentTreeDataSource source(tree, kTree);
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);
    feed.pumpListener();

    MediaItem *photo = itemCaptioned(setNamed(feed, "Pictures"), "IMG_1.jpg");
    CHECK(photo != nullptr);
    if (photo != nullptr) {
        source.prepareItem(photo, DataSource::ItemLoad::CachedThumbnail);
        source.prepareItem(photo, DataSource::ItemLoad::CachedThumbnail);
        CHECK_EQ(tree.thumbnailReads.load(), 1);
        CHECK(tree.lastThumbnailEdge.load() > 0 && tree.lastThumbnailEdge.load() < 256);
        CHECK_EQ(tree.exifReads.load(), 0);
        CHECK(photo->takeLateDetails());
        CHECK_EQ(photo->mRotation, 180.0f);
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
    CHECK_EQ(tree.folderQueries.load(), 1);
}
