#include "platform/android/AndroidDocumentTreeClient.h"

#include "platform/android/AndroidBridge.h"

std::string AndroidDocumentTreeClient::listFolder(const std::string &folderUri) {
    return AndroidBridge::listFolder(folderUri);
}

std::string AndroidDocumentTreeClient::readExif(const std::string &uri, const std::string &mime) {
    return AndroidBridge::readExif(uri, mime);
}

bool AndroidDocumentTreeClient::readThumbnail(const std::string &uri, int maxEdge, Bitmap *bitmap,
                                              int *orientation) {
    return AndroidBridge::readThumbnail(uri, maxEdge, bitmap, orientation);
}

bool AndroidDocumentTreeClient::readDocument(const std::string &uri, std::vector<uint8_t> *bytes) {
    return AndroidBridge::readDocument(uri, bytes);
}
