#include "fake_document_tree.h"

#include <nlohmann/json.hpp>

#include "graphics/Bitmap.h"

std::string FakeDocumentTree::listFolder(const std::string &requested) {
    ++folderQueries;
    if (onListFolder) {
        onListFolder(requested);
    }
    const bool root = requested == treeUri;
    const FakeFolder *folder = nullptr;
    for (const FakeFolder &candidate : folders) {
        if (root ? candidate.parentUri.empty() : (!candidate.parentUri.empty() && candidate.uri == requested)) {
            folder = &candidate;
            break;
        }
    }
    if (folder == nullptr) {
        // What the bridge answers for a folder it may no longer read.
        return std::string();
    }

    nlohmann::json answer = {{"folders", nlohmann::json::array()}, {"photos", nlohmann::json::array()}};
    if (root) {
        answer["name"] = folder->name;
    }
    for (const FakeFolder &child : folders) {
        if (child.parentUri == folder->uri) {
            answer["folders"].push_back({{"id", child.uri}, {"name", child.name}});
        }
    }
    for (const FakeDocument &document : documents) {
        if (document.folderUri == folder->uri) {
            answer["photos"].push_back({{"uri", document.uri},
                                        {"name", document.name},
                                        {"mime", document.mime},
                                        {"dateModified", document.dateModified}});
        }
    }
    return answer.dump();
}

std::string FakeDocumentTree::readExif(const std::string &uri, const std::string &mime) {
    (void)mime;
    ++exifReads;
    for (const FakeDocument &document : documents) {
        if (document.uri == uri) {
            return nlohmann::json({{"orientation", document.orientation},
                                   {"dateTaken", document.dateTaken},
                                   {"width", document.width},
                                   {"height", document.height}})
                .dump();
        }
    }
    return std::string();
}

bool FakeDocumentTree::readDocument(const std::string &uri, std::vector<uint8_t> *bytes) {
    for (const FakeDocument &document : documents) {
        if (document.uri == uri) {
            return !document.file.empty() && Bitmap::readFile(document.file, bytes);
        }
    }
    return false;
}
