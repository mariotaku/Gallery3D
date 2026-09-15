// Where the platform keeps the user's photos, when that is more than one
// folder: on Windows the Pictures library, which can take in folders on other
// drives, and the Camera Roll the camera saves into.
#pragma once

#include <string>
#include <vector>

namespace PhotoLibrary {

struct Locations {
    // Folders to walk for photos, none inside another. Empty where the
    // platform keeps no library of its own.
    std::vector<std::string> folders;
    // Folders the camera saves into. An album inside one is the camera's.
    std::vector<std::string> cameraRolls;
};

// Asks the platform. On Windows these are the Pictures library's folders and
// the Camera Roll, both the known folder and its library, with the camera roll
// walked too wherever it is. Elsewhere both lists are empty.
Locations systemLocations();

// How a platform spells paths. The functions below take one, defaulting to this
// platform's, so every platform's rules are tested on every platform.
enum class PathStyle {
    // Case counts, and only "/" separates.
    Posix,
    // Case does not count for ASCII letters, "\" and "/" both separate, and a
    // drive's root such as "c:/" is a folder in its own right.
    Windows,
};

// The style of the platform this build runs on.
PathStyle nativePathStyle();

// Whether path is root or lies somewhere under it, compared a component at a
// time so that "Photos 2" is not under "Photos".
bool isWithin(const std::string &path, const std::string &root, PathStyle style = nativePathStyle());

// The one spelling isWithin compares: forward slashes, no trailing separator,
// and for Windows lower case. Two spellings of one file give the same string.
std::string comparable(const std::string &path, PathStyle style = nativePathStyle());

// The folders without duplicates and without any that lies inside another,
// in the order given. A library may name a folder and one of its subfolders
// as well, and walking both would put every album under it on the wall twice.
std::vector<std::string> withoutNested(const std::vector<std::string> &folders,
                                       PathStyle style = nativePathStyle());

// Whether a file is a cloud placeholder whose contents are not on this disk,
// such as a Dropbox or OneDrive file kept online only. Reading any of it, even
// the EXIF header, makes the provider download the whole file, so a scan must
// not open one. Asks for attributes only, which downloads nothing. Always
// false off Windows.
bool isOnlineOnly(const std::string &path);

// The attribute test behind isOnlineOnly, taking Windows file attribute bits:
// recall on data access, recall on open, or the older offline flag.
bool needsDownload(unsigned long attributes);

}  // namespace PhotoLibrary
