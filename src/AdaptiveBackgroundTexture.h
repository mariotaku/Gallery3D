// Port of com.cooliris.media.AdaptiveBackgroundTexture.
//
// Builds the backdrop out of a photo: downsample the thumbnail, box blur it
// twice and darken it, so the wall sits on a wash of its own colours. The right
// edge fades out because BackgroundLayer stitches the result with a 25 percent
// overlap and needs the tiles to blend into each other.
#pragma once

#include "Texture.h"

class AdaptiveBackgroundTexture : public Texture {
  public:
    AdaptiveBackgroundTexture(TexturePtr base, int width, int height)
        : mBase(std::move(base)), mDestWidth(width), mDestHeight(height) {}

    bool isCached() const override {
        return true;
    }

    bool shouldQueue() const override {
        return true;
    }

    Bitmap load(RenderView *view) override;

  private:
    // The thumbnail this backdrop is derived from. Reloaded rather than read
    // back from GL, because the render view drops the bitmap after upload.
    TexturePtr mBase;
    int mDestWidth;
    int mDestHeight;
};
