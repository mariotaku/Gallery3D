#include "media/PhotoLibrary.h"

namespace PhotoLibrary {

bool isWithin(const std::string &path, const std::string &root) {
    const std::string normalizedPath = comparable(path);
    const std::string normalizedRoot = comparable(root);
    if (normalizedRoot.empty()) {
        return false;
    }
    if (normalizedPath == normalizedRoot) {
        return true;
    }
    if (normalizedPath.compare(0, normalizedRoot.size(), normalizedRoot) != 0) {
        return false;
    }
    // The root already ends in a separator only when it is a drive or "/".
    if (normalizedRoot.back() == '/') {
        return true;
    }
    return normalizedPath.size() > normalizedRoot.size() && normalizedPath[normalizedRoot.size()] == '/';
}

std::vector<std::string> withoutNested(const std::vector<std::string> &folders) {
    std::vector<std::string> kept;
    for (size_t i = 0; i < folders.size(); ++i) {
        bool covered = false;
        for (size_t j = 0; j < folders.size() && !covered; ++j) {
            if (i == j || !isWithin(folders[i], folders[j])) {
                continue;
            }
            // Inside another folder, or the same folder seen again: the first
            // of a set of duplicates is the one kept.
            const bool duplicate = isWithin(folders[j], folders[i]);
            covered = !duplicate || j < i;
        }
        if (!covered) {
            kept.push_back(folders[i]);
        }
    }
    return kept;
}

bool needsDownload(unsigned long attributes) {
    // Spelled out rather than taken from windows.h so the test holds on every
    // platform the tests run on.
    const unsigned long kOffline = 0x00001000UL;
    const unsigned long kRecallOnOpen = 0x00040000UL;
    const unsigned long kRecallOnDataAccess = 0x00400000UL;
    return (attributes & (kOffline | kRecallOnOpen | kRecallOnDataAccess)) != 0;
}

}  // namespace PhotoLibrary
