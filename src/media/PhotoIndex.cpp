#include "media/PhotoIndex.h"

#include <cmath>
#include <utility>

#include "media/PhotoLibrary.h"

namespace PhotoIndex {

std::string key(const std::string &path) {
    return PhotoLibrary::comparable(path);
}

int64_t unixMsFromOleDate(double date) {
    // Days since the end of 1899, of which 25569 fall before 1970.
    return (int64_t)std::llround((date - 25569.0) * 86400000.0);
}

void storedSize(unsigned orientation, int *width, int *height) {
    if (orientation >= 5 && orientation <= 8) {
        std::swap(*width, *height);
    }
}

std::string scopeClause(const std::vector<std::string> &folders) {
    std::string clause;
    for (const std::string &folder : folders) {
        std::string quoted;
        for (char c : folder) {
            // The index spells its scopes as file URLs, forward slashes only.
            const char spelled = (c == '\\') ? '/' : c;
            quoted.push_back(spelled);
            if (spelled == '\'') {
                quoted.push_back('\'');
            }
        }
        if (!clause.empty()) {
            clause += " OR ";
        }
        clause += "SCOPE='file:" + quoted + "'";
    }
    return clause.empty() ? clause : "(" + clause + ")";
}

}  // namespace PhotoIndex
