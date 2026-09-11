#include "FileOperations.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
// Included after windows.h, which it depends on.
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#else
#include <ctime>
#include <filesystem>
#include <fstream>
#endif

namespace {

#if !defined(_WIN32)

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

#endif

// Degrees to the EXIF orientation value. Only the three rotations the port
// understands; anything else means upright.
unsigned orientationForDegrees(float degrees) {
    int normalized = ((int)(degrees + 0.5f) % 360 + 360) % 360;
    switch (normalized) {
    case 90:
        return 6;
    case 180:
        return 3;
    case 270:
        return 8;
    default:
        return 1;
    }
}

}  // namespace

namespace FileOperations {

bool moveToTrash(const std::string &path) {
#if defined(_WIN32)
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
    // ALLOWUNDO sends files to the recycle bin; NOERRORUI and SILENT suppress shell dialogs.
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted) {
        SDL_Log("Could not recycle %s (code %d)", path.c_str(), result);
        return false;
    }
    return true;
#else
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
#endif
}

bool setExifOrientation(const std::string &path, float degrees) {
    // Find the orientation tag the same way Bitmap::readExif does, then write
    // over its value in place. Nothing else in the file moves.
    std::FILE *file = std::fopen(path.c_str(), "r+b");
    if (!file) {
        return false;
    }

    std::vector<uint8_t> header(65536);
    size_t read = std::fread(header.data(), 1, header.size(), file);
    if (read < 12 || header[0] != 0xFF || header[1] != 0xD8) {
        std::fclose(file);
        return false;
    }

    size_t pos = 2;
    while (pos + 4 <= read) {
        if (header[pos] != 0xFF) {
            break;
        }
        uint8_t marker = header[pos + 1];
        size_t length = ((size_t)header[pos + 2] << 8) | header[pos + 3];
        if (length < 2 || pos + 2 + length > read) {
            break;
        }
        if (marker == 0xE1 && length >= 16 && std::memcmp(&header[pos + 4], "Exif\0\0", 6) == 0) {
            const size_t tiffStart = pos + 10;
            const uint8_t *tiff = &header[tiffStart];
            size_t tiffLength = length - 8;
            bool bigEndian = tiff[0] == 'M';
            auto read16 = [&](size_t offset) -> unsigned {
                if (offset + 2 > tiffLength) {
                    return 0;
                }
                return bigEndian ? (unsigned)((tiff[offset] << 8) | tiff[offset + 1])
                                 : (unsigned)((tiff[offset + 1] << 8) | tiff[offset]);
            };
            auto read32 = [&](size_t offset) -> unsigned {
                if (offset + 4 > tiffLength) {
                    return 0;
                }
                if (bigEndian) {
                    return ((unsigned)tiff[offset] << 24) | ((unsigned)tiff[offset + 1] << 16) |
                           ((unsigned)tiff[offset + 2] << 8) | (unsigned)tiff[offset + 3];
                }
                return ((unsigned)tiff[offset + 3] << 24) | ((unsigned)tiff[offset + 2] << 16) |
                       ((unsigned)tiff[offset + 1] << 8) | (unsigned)tiff[offset];
            };

            unsigned ifdOffset = read32(4);
            unsigned entryCount = read16(ifdOffset);
            for (unsigned i = 0; i < entryCount; ++i) {
                size_t entry = (size_t)ifdOffset + 2 + (size_t)i * 12;
                if (entry + 12 > tiffLength) {
                    break;
                }
                if (read16(entry) != 0x0112) {
                    continue;
                }
                // A SHORT sits in the first two bytes of the value field.
                unsigned value = orientationForDegrees(degrees);
                uint8_t bytes[2];
                if (bigEndian) {
                    bytes[0] = (uint8_t)(value >> 8);
                    bytes[1] = (uint8_t)(value & 0xFF);
                } else {
                    bytes[0] = (uint8_t)(value & 0xFF);
                    bytes[1] = (uint8_t)(value >> 8);
                }
                long fileOffset = (long)(tiffStart + entry + 8);
                if (std::fseek(file, fileOffset, SEEK_SET) != 0 ||
                    std::fwrite(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
                    std::fclose(file);
                    return false;
                }
                std::fclose(file);
                return true;
            }
            break;
        }
        if (marker == 0xDA) {
            break;
        }
        pos += 2 + length;
    }

    std::fclose(file);
    return false;
}



bool openInDefaultApp(const std::string &path) {
#if defined(_WIN32)
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.length(), nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    std::vector<wchar_t> wide((size_t)wideLength + 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.length(), wide.data(), wideLength);
    // ShellExecute returns values above 32 on success, small error codes otherwise.
    HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.data(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
#else
    // Open the local file in the desktop's default application. The path
    // becomes a URL, so a space or a hash in a filename has to be encoded.
    std::error_code error;
    const fs::path absolute = fs::absolute(fs::path(path), error);
    if (error) {
        return false;
    }
    return SDL_OpenURL(("file://" + percentEncodePath(absolute.string())).c_str());
#endif
}

}  // namespace FileOperations
