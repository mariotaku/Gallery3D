// DocumentTreeClient through StorageBridge.java.
#pragma once

#include "media/DocumentTreeClient.h"

class AndroidDocumentTreeClient : public DocumentTreeClient {
  public:
    std::string listFolders(const std::string &treeUri) override;
    std::string listPhotos(const std::string &folderUri) override;
    bool readDocument(const std::string &uri, std::vector<uint8_t> *bytes) override;
};
