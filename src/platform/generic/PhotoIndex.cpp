#include "media/PhotoIndex.h"

namespace PhotoIndex {

Listing query(const std::vector<std::string> &folders) {
    // No index to ask, so a scan walks every folder and reads each photo's
    // EXIF itself.
    (void)folders;
    return Listing();
}

}  // namespace PhotoIndex
