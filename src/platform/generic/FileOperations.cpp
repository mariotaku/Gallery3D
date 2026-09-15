// Android and iOS, where a photo is not a file the app may move or hand to
// another app by path. There is no freedesktop.org trash to move it to, and
// SDL_OpenURL cannot open a file:// URI there, so both answer false and leave
// the file where it is.
#include "media/FileOperations.h"

namespace FileOperations {

bool moveToTrash(const std::string &path) {
    (void)path;
    return false;
}

bool openInDefaultApp(const std::string &path) {
    (void)path;
    return false;
}

}  // namespace FileOperations
