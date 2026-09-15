// Writing into a freedesktop.org thumbnail cache the way a thumbnailer does,
// for tests that read one back.
#pragma once

#include <string>

#include "graphics/Bitmap.h"

namespace FreedesktopCache {

// The file's modification time in whole seconds, which Thumb::MTime holds.
// Zero when the file cannot be read.
long long modifiedSeconds(const std::string &path);

// Files thumbnail for the photo at path under root, in the size folder, such
// as "large", with Thumb::URI and Thumb::MTime set.
bool put(const std::string &root, const std::string &path, const std::string &folder, const Bitmap &thumbnail,
         long long mtime);

}  // namespace FreedesktopCache
