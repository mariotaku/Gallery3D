#include "media/FileOperations.h"

#include <SDL3/SDL.h>

#include <vector>

#include <windows.h>
// Included after windows.h, which it depends on.
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")

namespace FileOperations {

bool moveToTrash(const std::string &path) {
    // SHFileOperation wants UTF-16 and a double null terminated list.
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.length(), nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    std::vector<wchar_t> wide((size_t)wideLength + 2, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.length(), wide.data(), wideLength);

    SHFILEOPSTRUCTW operation {};
    operation.wFunc = FO_DELETE;
    operation.pFrom = wide.data();
    // ALLOWUNDO sends files to the recycle bin; NOERRORUI and SILENT suppress
    // shell dialogs. A drive with no recycle bin, such as a network share,
    // would delete the file for good without a word, so WANTNUKEWARNING asks
    // first there, and a refusal leaves the file where it is.
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_NOERRORUI | FOF_SILENT;
    int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted) {
        SDL_Log("Could not recycle %s (code %d)", path.c_str(), result);
        return false;
    }
    return true;
}

bool openInDefaultApp(const std::string &path) {
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.length(), nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    std::vector<wchar_t> wide((size_t)wideLength + 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.length(), wide.data(), wideLength);
    // ShellExecute returns values above 32 on success, small error codes otherwise.
    HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.data(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

}  // namespace FileOperations
