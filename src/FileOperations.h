// File operations: recycle-bin deletion and in-place EXIF orientation updates.
// Deletion uses the Windows recycle bin and the freedesktop.org trash.
// Rotation does not re-encode pixels.
#pragma once

#include <string>

namespace FileOperations {

// Moves the file to the desktop's recycle bin. Returns false if it could not,
// in which case nothing is deleted.
bool moveToTrash(const std::string &path);

// Rewrites EXIF orientation. Without an orientation tag (PNG or EXIF-free JPEG),
// returns false and rotation remains session-only.
bool setExifOrientation(const std::string &path, float degrees);

// Opens the file in the desktop's default application, including videos.
// The Java wall also used an external player; SurfaceTexture arrived after this release.
bool openInDefaultApp(const std::string &path);

}  // namespace FileOperations
