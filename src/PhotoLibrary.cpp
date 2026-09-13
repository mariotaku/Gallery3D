#include "PhotoLibrary.h"

#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
// After windows.h, which these depend on.
#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace {

// One spelling per folder: forward slashes, no trailing separator, and on
// Windows lower case, so that two spellings of the same folder compare equal.
std::string normalized(std::string path) {
#if defined(_WIN32)
    std::replace(path.begin(), path.end(), '\\', '/');
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
        // ASCII only. Folding the rest needs the locale, and a folder whose
        // name differs only in the case of a non-ASCII letter is rare enough.
        return (char)((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    });
#endif
    // Keep the root itself whole: "/" and "c:/" are folders in their own right.
    while (path.size() > 1 && path.back() == '/' && !(path.size() == 3 && path[1] == ':')) {
        path.pop_back();
    }
    return path;
}

#if defined(_WIN32)

std::string utf8Of(const wchar_t *wide) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return std::string();
    }
    std::string text((size_t)length - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), length, nullptr, nullptr);
    return text;
}

void appendKnownFolder(REFKNOWNFOLDERID folder, std::vector<std::string> &out) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &path))) {
        out.push_back(utf8Of(path));
    }
    // Owed back even when the call fails.
    CoTaskMemFree(path);
}

// Every folder a library includes that is on a disk. A library can also hold
// locations with no file system path, such as a phone's storage, and those
// cannot be walked.
void appendLibraryFolders(REFKNOWNFOLDERID library, std::vector<std::string> &out) {
    IShellLibrary *shellLibrary = nullptr;
    if (FAILED(SHLoadLibraryFromKnownFolder(library, STGM_READ, IID_PPV_ARGS(&shellLibrary)))) {
        return;
    }
    IShellItemArray *items = nullptr;
    if (SUCCEEDED(shellLibrary->GetFolders(LFF_FORCEFILESYSTEM, IID_PPV_ARGS(&items)))) {
        DWORD count = 0;
        items->GetCount(&count);
        for (DWORD i = 0; i < count; ++i) {
            IShellItem *item = nullptr;
            if (FAILED(items->GetItemAt(i, &item))) {
                continue;
            }
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                out.push_back(utf8Of(path));
                CoTaskMemFree(path);
            }
            item->Release();
        }
        items->Release();
    }
    shellLibrary->Release();
}

#endif

}  // namespace

namespace PhotoLibrary {

std::string comparable(const std::string &path) {
    return normalized(path);
}

bool isWithin(const std::string &path, const std::string &root) {
    const std::string normalizedPath = normalized(path);
    const std::string normalizedRoot = normalized(root);
    if (normalizedRoot.empty()) {
        return false;
    }
    if (normalizedPath == normalizedRoot) {
        return true;
    }
    if (normalizedPath.compare(0, normalizedRoot.size(), normalizedRoot) != 0) {
        return false;
    }
    // The root already ends in a separator only when it is a drive or "/".
    if (normalizedRoot.back() == '/') {
        return true;
    }
    return normalizedPath.size() > normalizedRoot.size() && normalizedPath[normalizedRoot.size()] == '/';
}

std::vector<std::string> withoutNested(const std::vector<std::string> &folders) {
    std::vector<std::string> kept;
    for (size_t i = 0; i < folders.size(); ++i) {
        bool covered = false;
        for (size_t j = 0; j < folders.size() && !covered; ++j) {
            if (i == j || !isWithin(folders[i], folders[j])) {
                continue;
            }
            // Inside another folder, or the same folder seen again: the first
            // of a set of duplicates is the one kept.
            const bool duplicate = isWithin(folders[j], folders[i]);
            covered = !duplicate || j < i;
        }
        if (!covered) {
            kept.push_back(folders[i]);
        }
    }
    return kept;
}

bool needsDownload(unsigned long attributes) {
    // Spelled out rather than taken from windows.h so the test holds on every
    // platform the tests run on.
    const unsigned long kOffline = 0x00001000UL;
    const unsigned long kRecallOnOpen = 0x00040000UL;
    const unsigned long kRecallOnDataAccess = 0x00400000UL;
    return (attributes & (kOffline | kRecallOnOpen | kRecallOnDataAccess)) != 0;
}

bool isOnlineOnly(const std::string &path) {
#if defined(_WIN32)
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return false;
    }
    std::wstring wide((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), length);
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && needsDownload(attributes);
#else
    (void)path;
    return false;
#endif
}

Locations systemLocations() {
    Locations locations;
#if defined(_WIN32)
    // The shell's library objects are COM. SDL may already have initialized it
    // on this thread, which counts as a reference that is ours to release too;
    // a thread already in the other apartment model can still make these calls.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    appendLibraryFolders(FOLDERID_PicturesLibrary, locations.folders);
    if (locations.folders.empty()) {
        appendKnownFolder(FOLDERID_Pictures, locations.folders);
    }
    appendKnownFolder(FOLDERID_CameraRoll, locations.cameraRolls);
    appendLibraryFolders(FOLDERID_CameraRollLibrary, locations.cameraRolls);
    locations.cameraRolls = withoutNested(locations.cameraRolls);

    // A camera roll moved off the Pictures library still holds photos.
    locations.folders.insert(locations.folders.end(), locations.cameraRolls.begin(), locations.cameraRolls.end());
    locations.folders = withoutNested(locations.folders);

    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
#endif
    return locations;
}

}  // namespace PhotoLibrary
