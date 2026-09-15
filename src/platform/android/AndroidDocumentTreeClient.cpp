#include "platform/android/AndroidDocumentTreeClient.h"

#include "platform/android/AndroidBridge.h"

std::string AndroidDocumentTreeClient::listFolders(const std::string &treeUri) {
    return AndroidBridge::listFolders(treeUri);
}

std::string AndroidDocumentTreeClient::listPhotos(const std::string &folderUri) {
    return AndroidBridge::listPhotos(folderUri);
}

std::string AndroidDocumentTreeClient::readExif(const std::string &uri, const std::string &mime) {
    return AndroidBridge::readExif(uri, mime);
}

bool AndroidDocumentTreeClient::readDocument(const std::string &uri, std::vector<uint8_t> *bytes) {
    return AndroidBridge::readDocument(uri, bytes);
}
