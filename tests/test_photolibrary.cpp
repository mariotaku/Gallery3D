// Walking a library of several folders, and knowing which album is the
// camera's.
//
// Windows' Pictures library can name a folder and its subfolders at once, so
// the folders are narrowed to those no other one contains before anything is
// walked. The camera roll comes from the platform and is matched by path.
#include "tests.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "media/LocalDataSource.h"
#include "media/MediaFeed.h"
#include "media/MediaSet.h"
#include "media/PhotoLibrary.h"

namespace fs = std::filesystem;

namespace {

// An empty file with a photo's extension, which is all a scan looks at.
void touch(const fs::path &path) {
    fs::create_directories(path.parent_path());
    std::FILE *file = std::fopen(path.string().c_str(), "wb");
    if (file != nullptr) {
        std::fclose(file);
    }
}

// A tree of photos in the temp folder, removed again when the test is done.
struct TempTree {
    TempTree() : root(fs::temp_directory_path() / "gallery3d_photolibrary_test") {
        std::error_code error;
        fs::remove_all(root, error);
    }
    ~TempTree() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    std::string path(const std::string &relative) const {
        return (root / relative).string();
    }
    fs::path root;
};

}  // namespace

TEST(a_folder_is_within_itself_and_its_parents) {
    CHECK(PhotoLibrary::isWithin("/photos/2026/kyoto", "/photos"));
    CHECK(PhotoLibrary::isWithin("/photos/2026", "/photos/2026"));
    CHECK(PhotoLibrary::isWithin("/photos/2026/", "/photos/2026"));
    CHECK(PhotoLibrary::isWithin("/photos/2026", "/photos/"));
    CHECK(PhotoLibrary::isWithin("/anything", "/"));
    CHECK(!PhotoLibrary::isWithin("/photos", "/photos/2026"));
}

TEST(a_shared_prefix_is_not_a_parent) {
    // Compared a component at a time, so the second folder is not inside the
    // first just because its name starts the same way.
    CHECK(!PhotoLibrary::isWithin("/photos 2", "/photos"));
    CHECK(!PhotoLibrary::isWithin("/photosbackup/a", "/photos"));
    CHECK(!PhotoLibrary::isWithin("/photos", ""));
}

#if defined(_WIN32)
TEST(windows_paths_match_across_case_and_slashes) {
    // What the known folder hands back against what the walk produces.
    CHECK(PhotoLibrary::isWithin("C:\\Users\\someone\\Pictures\\Camera Roll\\2026",
                                 "c:/users/someone/pictures/camera roll"));
    CHECK(PhotoLibrary::isWithin("D:/Dropbox/Photos", "D:\\"));
    CHECK(!PhotoLibrary::isWithin("D:\\Dropbox", "C:\\"));
}
#endif

TEST(nested_and_repeated_library_folders_are_walked_once) {
    // The shape of a real Pictures library: a synced folder and some of its
    // own subfolders, and one folder named twice.
    const std::vector<std::string> folders = {
        "/dropbox/photos/2021-09-21", "/home/pictures", "/dropbox/photos", "/dropbox/photos/import",
        "/home/pictures",
    };
    const std::vector<std::string> kept = PhotoLibrary::withoutNested(folders);
    CHECK_EQ(kept.size(), (size_t)2);
    CHECK(kept.size() == 2 && kept[0] == "/home/pictures" && kept[1] == "/dropbox/photos");
}

TEST(a_cloud_placeholder_is_one_that_would_download) {
    // What Dropbox and OneDrive set on a file kept online only.
    CHECK(PhotoLibrary::needsDownload(0x00400000UL | 0x00001000UL | 0x00100000UL | 0x00000200UL));
    CHECK(PhotoLibrary::needsDownload(0x00040000UL));
    CHECK(PhotoLibrary::needsDownload(0x00001000UL));
    // Pinned, archive and sparse: a file already here, however it got here.
    CHECK(!PhotoLibrary::needsDownload(0x00080000UL | 0x00000020UL | 0x00000200UL));
    CHECK(!PhotoLibrary::needsDownload(0));
}

TEST(a_file_on_disk_is_not_online_only) {
    TempTree tree;
    touch(tree.path("here.jpg"));
    CHECK(!PhotoLibrary::isOnlineOnly(tree.path("here.jpg")));
    // Nothing there to ask about, which is not the same as needing a download.
    CHECK(!PhotoLibrary::isOnlineOnly(tree.path("missing.jpg")));
}

TEST(a_library_of_several_folders_shows_each_album_once) {
    TempTree tree;
    touch(tree.path("pictures/Camera Roll/IMG_0001.jpg"));
    touch(tree.path("pictures/Camera Roll/2026/IMG_0002.jpg"));
    touch(tree.path("pictures/Holiday/beach.jpg"));
    touch(tree.path("pictures/Holiday/notes.txt"));
    touch(tree.path("synced/Kyoto/temple.png"));

    // The library names Holiday on its own as well as the folder it is in.
    LocalDataSource source({tree.path("pictures"), tree.path("pictures/Holiday"), tree.path("synced")},
                           {tree.path("pictures/Camera Roll")});
    MediaFeed feed(&source, nullptr);
    source.loadMediaSets(&feed);

    std::vector<std::string> names;
    std::vector<std::string> cameraNames;
    for (MediaSet *set : feed.getMediaSets()) {
        names.push_back(set->mName);
        if (set->mIsCameraRoll) {
            cameraNames.push_back(set->mName);
        }
    }
    std::sort(names.begin(), names.end());
    std::sort(cameraNames.begin(), cameraNames.end());

    // Four folders hold photos; Holiday is not repeated for being named twice.
    CHECK_EQ(names.size(), (size_t)4);
    CHECK(std::count(names.begin(), names.end(), "Holiday") == 1);
    CHECK(std::count(names.begin(), names.end(), "Kyoto") == 1);
    // The camera roll and the folder inside it are both the camera's, and
    // nothing else is.
    CHECK(cameraNames.size() == 2 && cameraNames[0] == "2026" && cameraNames[1] == "Camera Roll");
}
