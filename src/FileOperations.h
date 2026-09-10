// The two things the port does to a user's files, kept in one place because
// both need to be careful and neither belongs in the media layer.
//
// Deleting goes to the recycle bin, never straight to unlink. These are
// somebody's photographs and the app offers no undo of its own, so the only
// safe delete is one the desktop can reverse. Where that is not implemented
// the operation refuses rather than falling back to a permanent delete.
//
// Rotating rewrites the EXIF orientation tag in place rather than re-encoding
// the image, so it costs two bytes and loses nothing.
#pragma once

#include <string>

namespace FileOperations {

// Moves the file to the desktop's recycle bin. Returns false if it could not,
// including on platforms where this is not implemented yet - in which case
// nothing is deleted.
bool moveToTrash(const std::string &path);

// Rewrites the EXIF orientation tag so the rotation survives a restart.
// Returns false when the file has no orientation tag to rewrite, which is the
// case for a PNG or a JPEG written without EXIF; the rotation then lives only
// as long as the session.
bool setExifOrientation(const std::string &path, float degrees);

// Hands the file to whatever the desktop opens it with. The port has no video
// decoder and the original had no player of its own either: MovieViewControl
// wrapped the platform one. This is the same idea, one level out.
bool openInDefaultApp(const std::string &path);

}  // namespace FileOperations
