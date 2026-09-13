#include "media/FileOperations.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

// Percent-encodes everything outside the unreserved set of RFC 3986, leaving
// the path separator alone. Both the trashinfo Path field and a file:// URL
// want this.
std::string percentEncodePath(const std::string &path) {
    static const char *kHexDigits = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(path.size());
    for (unsigned char character : path) {
        const bool unreserved = (character >= 'A' && character <= 'Z') ||
                                (character >= 'a' && character <= 'z') ||
                                (character >= '0' && character <= '9') || std::strchr("-_.~/", character) != nullptr;
        if (unreserved) {
            encoded.push_back((char)character);
        } else {
            encoded.push_back('%');
            encoded.push_back(kHexDigits[character >> 4]);
            encoded.push_back(kHexDigits[character & 0x0F]);
        }
    }
    return encoded;
}

// $XDG_DATA_HOME/Trash, or ~/.local/share/Trash when that is unset. Creates
// the files/ and info/ pair the spec requires.
bool homeTrashDirectory(fs::path &trash) {
    std::error_code error;
    if (const char *dataHome = SDL_getenv("XDG_DATA_HOME"); dataHome != nullptr && dataHome[0] != '\0') {
        trash = fs::path(dataHome) / "Trash";
    } else if (const char *home = SDL_getenv("HOME"); home != nullptr && home[0] != '\0') {
        trash = fs::path(home) / ".local" / "share" / "Trash";
    } else {
        return false;
    }
    fs::create_directories(trash / "files", error);
    fs::create_directories(trash / "info", error);
    return !error;
}

// The spec requires the name to be unique across both subdirectories, so a
// second photo.jpg does not take over the first one's info file.
bool reserveTrashName(const fs::path &trash, const std::string &stem, const std::string &extension,
                      fs::path &files, fs::path &info) {
    for (int attempt = 1; attempt < 1000; ++attempt) {
        std::string name = attempt == 1 ? stem + extension : stem + "." + std::to_string(attempt) + extension;
        files = trash / "files" / name;
        info = trash / "info" / (name + ".trashinfo");
        std::error_code error;
        if (!fs::exists(files, error) && !fs::exists(info, error)) {
            return true;
        }
    }
    return false;
}

}  // namespace

namespace FileOperations {

bool moveToTrash(const std::string &path) {
    // The freedesktop.org trash spec: move the file under Trash/files and
    // record where it came from in Trash/info, so a file manager can put it
    // back.
    std::error_code error;
    const fs::path original = fs::absolute(fs::path(path), error);
    // absolute() does not touch the disk, so ask separately. Checking here
    // keeps a missing file from leaving an info record behind.
    if (error || !fs::exists(original, error)) {
        SDL_Log("Could not resolve %s", path.c_str());
        return false;
    }

    fs::path trash;
    if (!homeTrashDirectory(trash)) {
        SDL_Log("Could not open the trash directory for %s", path.c_str());
        return false;
    }

    fs::path trashedFile;
    fs::path trashedInfo;
    if (!reserveTrashName(trash, original.stem().string(), original.extension().string(), trashedFile, trashedInfo)) {
        SDL_Log("Could not find a free name in the trash for %s", path.c_str());
        return false;
    }

    // The info file goes first. A files entry without one is an orphan that no
    // file manager will restore.
    {
        std::ofstream info(trashedInfo, std::ios::binary);
        if (!info) {
            SDL_Log("Could not write the trash record for %s", path.c_str());
            return false;
        }
        char deletionDate[32] = "";
        const std::time_t now = std::time(nullptr);
        std::tm parts {};
        if (localtime_r(&now, &parts) != nullptr) {
            std::strftime(deletionDate, sizeof(deletionDate), "%Y-%m-%dT%H:%M:%S", &parts);
        }
        info << "[Trash Info]\n"
             << "Path=" << percentEncodePath(original.string()) << "\n"
             << "DeletionDate=" << deletionDate << "\n";
    }

    fs::rename(original, trashedFile, error);
    if (error) {
        // The trash is on another filesystem, which rename cannot cross.
        std::error_code copyError;
        fs::copy_file(original, trashedFile, fs::copy_options::overwrite_existing, copyError);
        if (copyError || !fs::remove(original, copyError)) {
            fs::remove(trashedFile, copyError);
            fs::remove(trashedInfo, copyError);
            SDL_Log("Could not trash %s: %s", path.c_str(), error.message().c_str());
            return false;
        }
    }
    return true;
}

bool openInDefaultApp(const std::string &path) {
    // Open the local file in the desktop's default application. The path
    // becomes a URL, so a space or a hash in a filename has to be encoded.
    std::error_code error;
    const fs::path absolute = fs::absolute(fs::path(path), error);
    if (error) {
        return false;
    }
    return SDL_OpenURL(("file://" + percentEncodePath(absolute.string())).c_str());
}

}  // namespace FileOperations
