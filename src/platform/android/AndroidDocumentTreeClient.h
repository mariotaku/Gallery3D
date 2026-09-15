// DocumentTreeClient through StorageBridge.java.
#pragma once

#include "media/DocumentTreeClient.h"

class AndroidDocumentTreeClient : public DocumentTreeClient {
  public:
    std::string listFolder(const std::string &folderUri) override;
    std::string readExif(const std::string &uri, const std::string &mime) override;
    bool readThumbnail(const std::string &uri, int maxEdge, Bitmap *bitmap, int *orientation) override;
    bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) override;
};
