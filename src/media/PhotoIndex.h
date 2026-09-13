// What the platform has already read about the user's photos, so that a scan
// does not have to walk every folder and open every file to find it again. On
// Windows that is the Windows Search index, which lists each picture with its
// date, size, orientation, position and file attributes, the way MediaStore
// lists them on Android. Elsewhere there is nothing to ask, and a scan walks
// the folders and reads EXIF itself.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "graphics/Bitmap.h"

namespace PhotoIndex {

// One picture as the index holds it.
struct Entry {
    // The path as the file system spells it, for opening the file.
    std::string path;
    Bitmap::ExifInfo info;
    // The Windows file attributes the index last saw, or zero if it has none.
    unsigned long attributes = 0;
};

// Keyed by key() of each picture's path.
using Entries = std::unordered_map<std::string, Entry>;

struct Listing {
    Entries entries;
    // The folders asked about that the index covers whole. Every picture under
    // one of them is in entries, so a scan lists them from here instead of
    // walking them. Entries can still hold pictures under the other folders.
    std::vector<std::string> listedFolders;
};

// Everything the index holds on the pictures under the folders, in one query.
// Empty off Windows, and on Windows when the search service is off, which
// leaves a scan to walk every folder and read the files.
Listing query(const std::vector<std::string> &folders);

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
