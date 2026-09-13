#include "media/PhotoIndex.h"

namespace PhotoIndex {

Entries query(const std::vector<std::string> &folders) {
    // No index to ask, so a scan reads each photo's EXIF itself.
    (void)folders;
    return Entries();
}

}  // namespace PhotoIndex
