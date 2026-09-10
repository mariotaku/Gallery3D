#include "LocalDataSource.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>

#include "Bitmap.h"
#include "MediaFeed.h"
#include "MediaSet.h"

namespace fs = std::filesystem;

namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return text;
}

// A stable id per path, so a set keeps its identity across rescans.
int64_t hashPath(const std::string &path) {
    int64_t hash = 1469598103934665603LL;
    for (unsigned char c : path) {
        hash ^= (int64_t)c;
        hash *= 1099511628211LL;
    }
    return hash & 0x7FFFFFFFFFFFFFFFLL;
}

}  // namespace

bool LocalDataSource::isSupportedImage(const std::string &path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string extension = toLower(path.substr(dot));
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp" ||
           extension == ".gif" || extension == ".webp" || extension == ".tif" || extension == ".tiff";
}

std::string LocalDataSource::mimeTypeForPath(const std::string &path) {
    size_t dot = path.find_last_of('.');
    std::string extension = (dot == std::string::npos) ? "" : toLower(path.substr(dot));
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".gif") {
        return "image/gif";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    if (extension == ".bmp") {
        return "image/bmp";
    }
    if (extension == ".tif" || extension == ".tiff") {
        return "image/tiff";
    }
    return "image/jpeg";
}

void LocalDataSource::scan(const std::string &path, std::vector<Folder> &folders) const {
    std::error_code error;
    Folder folder;
    folder.path = path;
    folder.name = fs::path(path).filename().string();
    if (folder.name.empty()) {
        folder.name = path;
    }

    std::vector<std::string> subdirectories;
    for (const fs::directory_entry &entry : fs::directory_iterator(path, error)) {
        if (error) {
            break;
        }
        std::error_code entryError;
        if (entry.is_directory(entryError)) {
            std::string name = entry.path().filename().string();
            if (!name.empty() && name[0] == '.') {
                continue;
            }
            subdirectories.push_back(entry.path().string());
        } else if (entry.is_regular_file(entryError)) {
            std::string filePath = entry.path().string();
            if (isSupportedImage(filePath)) {
                folder.files.push_back(filePath);
            }
        }
    }

    if (!folder.files.empty()) {
        std::sort(folder.files.begin(), folder.files.end());
        folders.push_back(std::move(folder));
    }
    for (const std::string &subdirectory : subdirectories) {
        scan(subdirectory, folders);
    }
}

void LocalDataSource::loadMediaSets(MediaFeed *feed) {
    std::vector<Folder> folders;
    scan(mRootPath, folders);

    for (const Folder &folder : folders) {
        MediaSet *set = feed->addMediaSet(hashPath(folder.path));
        set->mName = folder.name;
        set->mType = MediaSet::TYPE_FOLDER;
        set->mIsLocal = true;

        for (const std::string &file : folder.files) {
            auto item = std::make_unique<MediaItem>();
            item->mId = hashPath(file);
            item->mFilePath = file;
            item->mContentUri = file;
            item->mThumbnailUri = file;
            item->mScreennailUri = file;
            item->mMimeType = mimeTypeForPath(file);
            item->mCaption = fs::path(file).filename().string();
            Bitmap::ExifInfo exif = Bitmap::readExif(file);
            item->mRotation = exif.rotationDegrees;

            std::error_code error;
            auto writeTime = fs::last_write_time(file, error);
            if (!error) {
                // file_clock's epoch is unspecified before C++20 and is not the
                // Unix epoch on Windows. Rather than hardcode the offset per
                // platform, carry the file time across to the system clock by
                // the difference between the two clocks read together. Costs a
                // little precision, which does not matter for a photo date, and
                // needs no guard.
                auto systemTime = std::chrono::system_clock::now() +
                                  (writeTime - std::filesystem::file_time_type::clock::now());
                int64_t unixSeconds =
                    std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
                item->mDateModifiedInSec = unixSeconds;
                item->mDateAddedInSec = unixSeconds;
                item->mDateTakenInMs = unixSeconds * 1000LL;
            }
            // The shot time beats the mtime, which is only when the file was
            // copied. Files without a readable EXIF date keep the mtime.
            if (exif.dateTakenMs != 0) {
                item->mDateTakenInMs = exif.dateTakenMs;
            }
            set->addItem(std::move(item));
        }
        set->updateNumExpectedItems();
        set->generateTitle(true);
        feed->updateListener(true);
    }
}

void LocalDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    (void)feed;
    (void)parentSet;
    // loadMediaSets already filled every set in.
}
