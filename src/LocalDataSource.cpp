#include "LocalDataSource.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>

#include "Bitmap.h"
#include "FileOperations.h"
#include "MediaFeed.h"
#include "MediaItem.h"
#include "MediaSet.h"
#include "RegionDecoder.h"

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

namespace {

// std::filesystem::path::string() converts to the process code page on Windows,
// which turns anything outside it into question marks: a folder named in
// Japanese arrives as a row of them. Everything past here is UTF-8, which is
// also what SDL reads paths as.
std::string utf8Of(const fs::path &path) {
    const auto wide = path.u8string();
    return std::string(wide.begin(), wide.end());
}

// And back. Constructing a path from a narrow string reads it as the code page
// too, so a UTF-8 one has to say so or it throws on the first byte it cannot
// make sense of.
fs::path pathOf(const std::string &utf8) {
    return fs::u8path(utf8);
}

}  // namespace

std::string LocalDataSource::folderDisplayName(const std::string &path) {
    fs::path trimmed = pathOf(path);
    if (!trimmed.has_filename()) {
        // Ends in a separator, so its last component is the parent's filename.
        trimmed = trimmed.parent_path();
    }
    const std::string name = utf8Of(trimmed.filename());
    // A root has no component of its own to be named after.
    return name.empty() ? path : name;
}

void LocalDataSource::scan(const std::string &path, std::vector<Folder> &folders) const {
    std::error_code error;
    Folder folder;
    folder.path = path;
    folder.name = folderDisplayName(path);

    std::vector<std::string> subdirectories;
    for (const fs::directory_entry &entry : fs::directory_iterator(pathOf(path), error)) {
        if (error) {
            break;
        }
        std::error_code entryError;
        if (entry.is_directory(entryError)) {
            std::string name = utf8Of(entry.path().filename());
            if (!name.empty() && name[0] == '.') {
                continue;
            }
            subdirectories.push_back(utf8Of(entry.path()));
        } else if (entry.is_regular_file(entryError)) {
            std::string filePath = utf8Of(entry.path());
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
        MediaSet *set = feed->addMediaSet(hashPath(folder.path), this);
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
            item->mCaption = utf8Of(pathOf(file).filename());
            Bitmap::ExifInfo exif = Bitmap::readExif(file);
            item->mRotation = exif.rotationDegrees;
            item->mLatitude = exif.latitude;
            item->mLongitude = exif.longitude;
            // The full size is what tells the fullscreen view a photo is worth
            // tiling, and how many tiles it takes.
            item->mFullWidth = exif.pixelWidth;
            item->mFullHeight = exif.pixelHeight;

            std::error_code error;
            auto writeTime = fs::last_write_time(pathOf(file), error);
            if (!error) {
                // file_clock's epoch is unspecified before C++20 and differs from Unix on
                // Windows.
                // Convert using the offset between simultaneous file and system clock readings.
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
        // Sort by date after reading EXIF; directory enumeration is in filename order.
        set->sortItemsByDate();
        set->updateNumExpectedItems();
        set->generateTitle(true);
        feed->updateListener(true);
    }
    // Everything is on this disk, so the page is complete the moment the scan
    // is.
    feed->finishLoadingMediaSets();
}

void LocalDataSource::loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) {
    // loadMediaSets completes every local set; mark pagination finished immediately.
    if (feed != nullptr) {
        feed->finishLoadingItemsForSet(parentSet);
    }
}

bool LocalDataSource::performOperation(int operation, MediaItem *item, const void *data) {
    if (item == nullptr || item->mFilePath.empty()) {
        return false;
    }
    switch (operation) {
    case MediaFeed::OPERATION_DELETE:
        // Report deletion success only after the file reaches the recycle bin.
        return FileOperations::moveToTrash(item->mFilePath);
    case MediaFeed::OPERATION_ROTATE: {
        // A PNG or an EXIF free JPEG has no tag to rewrite, so the turn lasts
        // only as long as the session and this says so.
        float degrees = (data != nullptr) ? *(const float *)data : 0.0f;
        return FileOperations::setExifOrientation(item->mFilePath, degrees);
    }
    default:
        return false;
    }
}

bool LocalDataSource::supportsOperation(int operation) const {
    // Local files support recycling and EXIF rotation.
    return operation == MediaFeed::OPERATION_DELETE || operation == MediaFeed::OPERATION_ROTATE;
}

bool LocalDataSource::supportsRegions(const MediaItem *item) const {
    // Asked once per frame while a photo is fullscreen, so it reads no file.
    // Opening the decoder is what decides for certain, and requestRegion falls
    // back to nothing if that fails.
    return item != nullptr && !item->mFilePath.empty() && RegionDecoder::looksSupported(item->mMimeType);
}

void LocalDataSource::requestRegion(MediaItem *item, int x, int y, int width, int height, int outWidth,
                                    int outHeight, RegionCallback done) {
    if (item == nullptr || !item->hasFullSize() || width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0) {
        done(Bitmap());
        return;
    }
    // Clamp to the image. The caller sizes the output for the trimmed region.
    x = std::max(0, x);
    y = std::max(0, y);
    width = std::min(width, item->mFullWidth - x);
    height = std::min(height, item->mFullHeight - y);
    if (width <= 0 || height <= 0) {
        done(Bitmap());
        return;
    }
    RegionDecoderPtr decoder = mDecoders.get(item->mFilePath);
    if (!decoder) {
        done(Bitmap());
        return;
    }
    // The decode pool already runs this off the render thread, so it answers
    // before returning rather than queueing work of its own.
    done(decoder->decodeRegion(x, y, width, height, outWidth, outHeight));
}
