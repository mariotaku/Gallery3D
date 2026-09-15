#include "media/PhotoLibrary.h"

#include <algorithm>

namespace PhotoLibrary {

std::string comparable(const std::string &path, PathStyle style) {
    std::string spelled = path;
    if (style == PathStyle::Windows) {
        std::replace(spelled.begin(), spelled.end(), '\\', '/');
        std::transform(spelled.begin(), spelled.end(), spelled.begin(), [](unsigned char c) {
            // ASCII only. Folding the rest needs the locale, and a folder whose
            // name differs only in the case of a non-ASCII letter is rare enough.
            return (char)((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
        });
        // Keep a drive's root whole: "c:/" is a folder in its own right.
        while (spelled.size() > 1 && spelled.back() == '/' && !(spelled.size() == 3 && spelled[1] == ':')) {
            spelled.pop_back();
        }
        return spelled;
    }
    // Case counts on these file systems, and a backslash is part of a name
    // rather than a separator, so only a trailing separator comes off. Keep
    // the root itself whole: "/" is a folder in its own right.
    while (spelled.size() > 1 && spelled.back() == '/') {
        spelled.pop_back();
    }
    return spelled;
}

bool isWithin(const std::string &path, const std::string &root, PathStyle style) {
    const std::string normalizedPath = comparable(path, style);
    const std::string normalizedRoot = comparable(root, style);
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

std::vector<std::string> withoutNested(const std::vector<std::string> &folders, PathStyle style) {
    std::vector<std::string> kept;
    for (size_t i = 0; i < folders.size(); ++i) {
        bool covered = false;
        for (size_t j = 0; j < folders.size() && !covered; ++j) {
            if (i == j || !isWithin(folders[i], folders[j], style)) {
                continue;
            }
            // Inside another folder, or the same folder seen again: the first
            // of a set of duplicates is the one kept.
            const bool duplicate = isWithin(folders[j], folders[i], style);
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
