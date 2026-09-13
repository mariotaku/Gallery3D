// Taking photo metadata from the platform's index instead of each file.
//
// The Windows Search query itself needs the service and an indexed folder, so
// what is covered here is the arithmetic around it and the choice a scan makes
// between the index's answer and the file's.
#include "tests.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "media/LocalDataSource.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "media/PhotoIndex.h"

namespace fs = std::filesystem;

namespace {

fs::path emptyPhoto(const fs::path &folder, const std::string &name) {
    fs::create_directories(folder);
    const fs::path path = folder / name;
    std::FILE *file = std::fopen(path.string().c_str(), "wb");
    if (file != nullptr) {
        std::fclose(file);
    }
    return path;
}

MediaItem *itemNamed(MediaFeed &feed, const std::string &caption) {
    for (MediaSet *set : feed.getMediaSets()) {
        for (int i = 0; i < set->getNumItems(); ++i) {
            MediaItem *item = set->getItems()[(size_t)i];
            if (item->mCaption == caption) {
                return item;
            }
        }
    }
    return nullptr;
}

}  // namespace

TEST(an_ole_date_counts_from_the_end_of_1899) {
    CHECK_EQ(PhotoIndex::unixMsFromOleDate(25569.0), (int64_t)0);
    CHECK_EQ(PhotoIndex::unixMsFromOleDate(25569.5), (int64_t)43200000);
    // 2025-04-11 01:43:56 UTC, which the index gave for a photo taken at
    // 10:43:56 in Tokyo.
    const double taken = 25569.0 + 1744335836.0 / 86400.0;
    CHECK_NEAR((double)PhotoIndex::unixMsFromOleDate(taken), 1744335836000.0, 1.0);
}

TEST(orientation_turns_the_same_way_from_the_index_and_from_exif) {
    CHECK_EQ(Bitmap::degreesForOrientation(1), 0.0f);
    CHECK_EQ(Bitmap::degreesForOrientation(3), 180.0f);
    CHECK_EQ(Bitmap::degreesForOrientation(6), 90.0f);
    CHECK_EQ(Bitmap::degreesForOrientation(8), 270.0f);
    CHECK_EQ(Bitmap::degreesForOrientation(2), 0.0f);
    CHECK_EQ(Bitmap::degreesForOrientation(0), 0.0f);
}

TEST(a_quarter_turned_picture_gets_its_stored_size_back) {
    // A camera held upright writes 7008x4672 pixels with orientation 8, and
    // the index reports that picture as 4672x7008.
    int width = 4672;
    int height = 7008;
    PhotoIndex::storedSize(8, &width, &height);
    CHECK_EQ(width, 7008);
    CHECK_EQ(height, 4672);
    for (unsigned orientation : {5u, 6u, 7u}) {
        width = 4672;
        height = 7008;
        PhotoIndex::storedSize(orientation, &width, &height);
        CHECK_EQ(width, 7008);
    }
    for (unsigned orientation : {0u, 1u, 2u, 3u, 4u, 9u}) {
        width = 7008;
        height = 4672;
        PhotoIndex::storedSize(orientation, &width, &height);
        CHECK_EQ(width, 7008);
        CHECK_EQ(height, 4672);
    }
}

TEST(the_scope_names_every_folder_as_a_file_url) {
    CHECK(PhotoIndex::scopeClause({}).empty());
    CHECK(PhotoIndex::scopeClause({"C:\\Users\\someone\\Pictures", "D:/Dropbox/Photos"}) ==
          "(SCOPE='file:C:/Users/someone/Pictures' OR SCOPE='file:D:/Dropbox/Photos')");
    // A quote in a folder's name would end the string early.
    CHECK(PhotoIndex::scopeClause({"D:/Mario's photos"}) == "(SCOPE='file:D:/Mario''s photos')");
}

TEST(a_scan_takes_what_the_index_knows_and_reads_the_rest) {
    const fs::path root = fs::temp_directory_path() / "gallery3d_photoindex_test";
    std::error_code error;
    fs::remove_all(root, error);
    const fs::path indexed = emptyPhoto(root / "Kyoto", "indexed.jpg");
    emptyPhoto(root / "Kyoto", "unindexed.jpg");
    emptyPhoto(root / "Kyoto", "sizeless.jpg");

    std::vector<std::string> askedFor;
    LocalDataSource source({root.string()}, {});
    source.setMetadataLookup([&](const std::vector<std::string> &folders) {
        askedFor = folders;
        PhotoIndex::Entries entries;
        Bitmap::ExifInfo known;
        known.dateTakenMs = 1744335836000LL;
        known.pixelWidth = 4000;
        known.pixelHeight = 3000;
        known.rotationDegrees = 90.0f;
        known.latitude = 35.6845;
        known.longitude = 139.8398;
        entries[PhotoIndex::key(indexed.string())] = known;
        // An entry the index has not read the picture for.
        Bitmap::ExifInfo sizeless;
        sizeless.rotationDegrees = 180.0f;
        entries[PhotoIndex::key((root / "Kyoto" / "sizeless.jpg").string())] = sizeless;
        return entries;
    });
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);

    CHECK(askedFor.size() == 1 && askedFor[0] == root.string());

    const MediaItem *fromIndex = itemNamed(feed, "indexed.jpg");
    CHECK(fromIndex != nullptr);
    if (fromIndex != nullptr) {
        CHECK_EQ(fromIndex->mDateTakenInMs, (int64_t)1744335836000LL);
        CHECK_EQ(fromIndex->mFullWidth, 4000);
        CHECK_EQ(fromIndex->mFullHeight, 3000);
        CHECK_EQ(fromIndex->mRotation, 90.0f);
        CHECK_NEAR(fromIndex->mLatitude, 35.6845, 1e-9);
    }

    // Neither of these has an answer worth taking, so both were read from an
    // empty file: no size, and no turn, whatever the index said.
    for (const char *name : {"unindexed.jpg", "sizeless.jpg"}) {
        const MediaItem *fromFile = itemNamed(feed, name);
        CHECK(fromFile != nullptr);
        if (fromFile != nullptr) {
            CHECK_EQ(fromFile->mFullWidth, 0);
            CHECK_EQ(fromFile->mRotation, 0.0f);
        }
    }
    fs::remove_all(root, error);
}
