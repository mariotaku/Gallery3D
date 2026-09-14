#include "media/LocalDataSource.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>

#include "graphics/Bitmap.h"
#include "graphics/SystemThumbnail.h"
#include "media/FileOperations.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "media/PhotoLibrary.h"
#include "graphics/RegionDecoder.h"

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
    // Icons, cursors and textures decode too, but are not photos.
    if (extension == ".ico" || extension == ".icon" || extension == ".cur" || extension == ".dds") {
        return false;
    }
    return Bitmap::decodesExtension(extension);
}

std::string LocalDataSource::mimeTypeForPath(const std::string &path) {
    size_t dot = path.find_last_of('.');
    std::string extension = (dot == std::string::npos) ? "" : toLower(path.substr(dot));
    if (extension.empty() || extension == ".jpg" || extension == ".jpeg" || extension == ".jpe" ||
        extension == ".jfif") {
        return "image/jpeg";
    }
    if (extension == ".tif" || extension == ".tiff") {
        return "image/tiff";
    }
    if (extension == ".heic" || extension == ".heif" || extension == ".hif") {
        return "image/heif";
    }
    // PNG, GIF, WebP, BMP, AVIF and the camera RAW formats go by the extension,
    // which is also how the details sheet names them: image/arw reads ARW.
    return "image/" + extension.substr(1);
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
            // A cloud placeholder is left off the wall: reading its EXIF here,
            // or its pixels later, would download the whole file.
            if (isSupportedImage(filePath) && !PhotoLibrary::isOnlineOnly(filePath)) {
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

namespace {

// A folder's path as components, which compare the way scan visits folders: a
// folder before everything inside it, and siblings by name.
std::vector<std::string> walkOrderOf(const std::string &path) {
    const std::string spelled = PhotoLibrary::comparable(path);
    std::vector<std::string> components;
    size_t start = 0;
    while (start < spelled.size()) {
        size_t end = spelled.find('/', start);
        if (end == std::string::npos) {
            end = spelled.size();
        }
        if (end > start) {
            components.push_back(spelled.substr(start, end - start));
        }
        start = end + 1;
    }
    return components;
}

// Whether folder lies under root through a folder whose name starts with a dot,
// which scan does not go into.
bool underHiddenFolder(const std::string &folder, const std::string &root) {
    const std::vector<std::string> components = walkOrderOf(folder);
    for (size_t i = walkOrderOf(root).size(); i < components.size(); ++i) {
        if (components[i][0] == '.') {
            return true;
        }
    }
    return false;
}

}  // namespace

void LocalDataSource::list(const std::string &root, const PhotoIndex::Entries &entries,
                           std::vector<Folder> &folders) const {
    struct Found {
        std::vector<std::string> order;
        std::string folder;
        std::string file;
    };
    std::vector<Found> found;
    for (const auto &indexed : entries) {
        const PhotoIndex::Entry &entry = indexed.second;
        // What scan keeps: pictures this build decodes, and no cloud
        // placeholder, whose attributes the index holds as well.
        if (!PhotoLibrary::isWithin(entry.path, root) || !isSupportedImage(entry.path) ||
            PhotoLibrary::needsDownload(entry.attributes)) {
            continue;
        }
        std::string folder = utf8Of(pathOf(entry.path).parent_path());
        if (underHiddenFolder(folder, root)) {
            continue;
        }
        std::vector<std::string> order = walkOrderOf(folder);
        found.push_back({std::move(order), std::move(folder), entry.path});
    }
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        return (a.order != b.order) ? (a.order < b.order) : (a.file < b.file);
    });

    const std::vector<std::string> *current = nullptr;
    for (Found &picture : found) {
        if (current == nullptr || *current != picture.order) {
            Folder folder;
            folder.path = picture.folder;
            folder.name = folderDisplayName(picture.folder);
            folders.push_back(std::move(folder));
            current = &picture.order;
        }
        folders.back().files.push_back(std::move(picture.file));
    }
}

void LocalDataSource::loadMediaSets(MediaFeed *feed) {
    const Uint64 started = SDL_GetTicks();
    const std::vector<std::string> roots = PhotoLibrary::withoutNested(mRoots);
    // One question to the platform for every photo at once. The folders it
    // covers are listed from its answer, and only the rest are walked.
    const PhotoIndex::Listing index = mIndexLookup ? mIndexLookup(roots) : PhotoIndex::Listing();
    const Uint64 asked = SDL_GetTicks();
    std::vector<Folder> folders;
    size_t listed = 0;
    for (const std::string &root : roots) {
        const bool covered =
            std::any_of(index.listedFolders.begin(), index.listedFolders.end(),
                        [&root](const std::string &folder) { return PhotoLibrary::isWithin(root, folder); });
        if (covered) {
            list(root, index.entries, folders);
            ++listed;
        } else {
            scan(root, folders);
        }
    }
    SDL_Log("Asked the index in %u ms, then listed %zu of %zu locations from it and walked the rest in %u ms",
            (unsigned)(asked - started), listed, roots.size(), (unsigned)(SDL_GetTicks() - asked));
    size_t fromIndex = 0;
    size_t fromFiles = 0;

    for (const Folder &folder : folders) {
        // Filled here and handed to the feed whole once its items are in.
        auto set = std::make_unique<MediaSet>();
        set->mId = hashPath(folder.path);
        set->mDataSource = this;
        set->mName = folder.name;
        set->mType = MediaSet::TYPE_FOLDER;
        set->mIsLocal = true;
        set->mIsCameraRoll = std::any_of(mCameraRolls.begin(), mCameraRolls.end(), [&folder](const std::string &roll) {
            return PhotoLibrary::isWithin(folder.path, roll);
        });

        for (const std::string &file : folder.files) {
            auto item = std::make_unique<MediaItem>();
            item->mId = hashPath(file);
            item->mFilePath = file;
            item->mContentUri = file;
            item->mThumbnailUri = file;
            item->mScreennailUri = file;
            item->mMimeType = mimeTypeForPath(file);
            item->mCaption = utf8Of(pathOf(file).filename());
            // The index's answer when it has one worth using. Without a pixel
            // size it has not read the picture, and the file is read instead.
            Bitmap::ExifInfo exif;
            const auto indexed = index.entries.find(PhotoIndex::key(file));
            if (indexed != index.entries.end() && indexed->second.info.pixelWidth > 0 &&
                indexed->second.info.pixelHeight > 0) {
                exif = indexed->second.info;
                ++fromIndex;
            } else {
                exif = Bitmap::readExif(file);
                ++fromFiles;
            }
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
        feed->addMediaSet(std::move(set));
    }
    SDL_Log("Read %zu photos in %u ms: %zu from the index, %zu from their files", fromIndex + fromFiles,
            (unsigned)(SDL_GetTicks() - started), fromIndex, fromFiles);
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

bool LocalDataSource::readThumbnail(MediaItem *item, int maxEdge, Bitmap *bitmap) {
    if (item == nullptr || item->mFilePath.empty() || bitmap == nullptr) {
        return false;
    }
    // A picture no larger than the request decodes whole, and a thumbnail of
    // it could only match that or be enlarged.
    if (item->hasFullSize() && std::max(item->mFullWidth, item->mFullHeight) <= maxEdge) {
        return false;
    }
    *bitmap = SystemThumbnail::load(item->mFilePath, maxEdge);
    return bitmap->valid();
}
