// Port of com.cooliris.media.DisplaySlot.
// Shares title and location textures by caption string.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "graphics/Texture.h"

class MediaSet;

class DisplaySlot {
  public:
    void setMediaSet(MediaSet *set);

    MediaSet *getMediaSet() const {
        return mSetRef;
    }

    bool hasValidLocation() const;

    std::shared_ptr<StringTexture> getTitleImage(std::map<std::string, std::shared_ptr<StringTexture>> &textureTable);
    std::shared_ptr<StringTexture> getLocationImage(std::map<std::string, std::shared_ptr<StringTexture>> &textureTable);

    static void initStyles();

  private:
    std::shared_ptr<StringTexture> getTextureForString(const std::string &text,
                                                       std::map<std::string, std::shared_ptr<StringTexture>> &table,
                                                       const StringTexture::Config &config);

    MediaSet *mSetRef = nullptr;
    std::string mTitle;
    std::shared_ptr<StringTexture> mTitleImage;
    std::string mLocation;
    std::shared_ptr<StringTexture> mLocationImage;
};
