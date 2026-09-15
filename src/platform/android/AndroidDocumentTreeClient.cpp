#include "platform/android/AndroidDocumentTreeClient.h"

#include "platform/android/AndroidBridge.h"

std::string AndroidDocumentTreeClient::listFolders(const std::string &treeUri) {
    return AndroidBridge::listFolders(treeUri);
}

std::string AndroidDocumentTreeClient::listPhotos(const std::string &folderUri) {
    return AndroidBridge::listPhotos(folderUri);
}

bool AndroidDocumentTreeClient::readDocument(const std::string &uri, std::vector<uint8_t> *bytes) {
    return AndroidBridge::readDocument(uri, bytes);
}
