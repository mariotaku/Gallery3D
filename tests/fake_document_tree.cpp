#include "fake_document_tree.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "graphics/Bitmap.h"

std::string FakeDocumentTree::listFolders(const std::string &requested) {
    if (requested != treeUri) {
        // What the bridge answers for a tree it may no longer read.
        return std::string();
    }
    nlohmann::json folders = nlohmann::json::array();
    for (const FakeDocument &document : documents) {
        auto found = std::find_if(folders.begin(), folders.end(), [&document](const nlohmann::json &folder) {
            return folder["id"] == document.folderUri;
        });
        if (found == folders.end()) {
            folders.push_back({{"id", document.folderUri}, {"name", document.folderName}, {"count", 1}});
        } else {
            (*found)["count"] = (*found)["count"].get<int>() + 1;
        }
    }
    return folders.dump();
}

std::string FakeDocumentTree::listPhotos(const std::string &folderUri) {
    ++photoQueries;
    nlohmann::json photos = nlohmann::json::array();
    for (const FakeDocument &document : documents) {
        if (document.folderUri != folderUri) {
            continue;
        }
        photos.push_back({{"uri", document.uri},
                          {"name", document.name},
                          {"mime", document.mime},
                          {"dateTaken", document.dateTaken},
                          {"dateModified", document.dateModified},
                          {"orientation", document.orientation},
                          {"width", document.width},
                          {"height", document.height}});
    }
    return photos.dump();
}

bool FakeDocumentTree::readDocument(const std::string &uri, std::vector<uint8_t> *bytes) {
    for (const FakeDocument &document : documents) {
        if (document.uri == uri) {
            return !document.file.empty() && Bitmap::readFile(document.file, bytes);
        }
    }
    return false;
}
