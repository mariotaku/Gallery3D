// What a folder is called on the wall.
//
// SDL hands out the user's folders with a separator on the end, and a path
// that ends in one has no filename component of its own. Taken at face value
// that left the whole path as the album's name, which is what the breadcrumb
// then showed.
#include "tests.h"

#include <string>

#include "media/LocalDataSource.h"

namespace {

std::string nameOf(const std::string &path) {
    return LocalDataSource::folderDisplayName(path);
}

}  // namespace

TEST(a_folder_is_called_after_its_last_component) {
    CHECK(nameOf("/home/someone/Pictures/Holiday") == "Holiday");
}

TEST(a_separator_on_the_end_is_not_part_of_the_name) {
    // How SDL_GetUserFolder returns them, which is what put a whole path on
    // the wall instead of a name.
    CHECK(nameOf("/home/someone/Pictures/") == "Pictures");
}

#if defined(_WIN32)
// A backslash separates on Windows and is an ordinary character in a name
// everywhere else, so these only hold where they are read that way.
TEST(a_backslash_path_is_read_the_windows_way) {
    CHECK(nameOf("C:\\Users\\someone\\Pictures\\Holiday") == "Holiday");
    CHECK(nameOf("C:\\Users\\someone\\Pictures\\") == "Pictures");
    // Forward slashes separate on Windows too, and paths arrive from settings
    // and the command line written either way.
    CHECK(nameOf("C:/Users/someone/Pictures/") == "Pictures");
}
#endif

TEST(a_name_with_spaces_and_dots_survives_whole) {
    CHECK(nameOf("/photos/Trip to Kyoto 2026") == "Trip to Kyoto 2026");
    CHECK(nameOf("/photos/photos.backup/") == "photos.backup");
}

TEST(a_root_keeps_whatever_it_was_given) {
    // Nothing better to call it, and an empty label would be worse.
    CHECK(!nameOf("/").empty());
}
