#include "media/PhotoLibrary.h"

#include <algorithm>

#include <windows.h>
// After windows.h, which these depend on.
#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

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

}  // namespace

namespace PhotoLibrary {

std::string comparable(const std::string &path) {
    std::string spelled = path;
    std::replace(spelled.begin(), spelled.end(), '\\', '/');
    std::transform(spelled.begin(), spelled.end(), spelled.begin(), [](unsigned char c) {
        // ASCII only. Folding the rest needs the locale, and a folder whose
        // name differs only in the case of a non-ASCII letter is rare enough.
        return (char)((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    });
    // Keep a drive's root whole: "c:/" is a folder in its own right.
    while (spelled.size() > 1 && spelled.back() == '/' && !(spelled.size() == 3 && spelled[1] == ':')) {
        spelled.pop_back();
    }
    return spelled;
}

bool isOnlineOnly(const std::string &path) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return false;
    }
    std::wstring wide((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), length);
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && needsDownload(attributes);
}

Locations systemLocations() {
    Locations locations;
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
    return locations;
}

}  // namespace PhotoLibrary
