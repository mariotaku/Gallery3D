// What the platform has already read about the user's photos, so that a scan
// does not open every file to read it again. On Windows that is the Windows
// Search index, which holds each picture's date, size, orientation and
// position; elsewhere there is nothing to ask and the scan reads EXIF itself.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "graphics/Bitmap.h"

namespace PhotoIndex {

// What the index knows about each picture, keyed by key() of its path.
using Entries = std::unordered_map<std::string, Bitmap::ExifInfo>;

// Everything the index holds on the pictures under the folders, in one query.
// Empty off Windows, and on Windows when the search service is off or the
// folders are not indexed, which leaves a scan to read the files.
Entries query(const std::vector<std::string> &folders);

// The spelling of a path that entries are keyed by. The index and a directory
// walk can write the same file differently, in case or in slashes.
std::string key(const std::string &path);

// An OLE automation date, which is how the index hands over a moment in UTC,
// as milliseconds since the Unix epoch.
int64_t unixMsFromOleDate(double date);

// The size the file stores its pixels at, from the size the index holds. The
// index gives the picture's size as it is shown, so for an EXIF orientation of
// 5 to 8, which turn it a quarter, the two edges are swapped back.
void storedSize(unsigned orientation, int *width, int *height);

// The WHERE clause that limits a query to the folders and everything under
// them, with quotes in a folder's name doubled as the query language wants.
std::string scopeClause(const std::vector<std::string> &folders);

}  // namespace PhotoIndex
