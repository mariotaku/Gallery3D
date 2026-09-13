// Port of com.cooliris.media.AdaptiveBackgroundTexture.
// Blur and darken a thumbnail; fade its right edge for BackgroundLayer's 25% overlap.
#pragma once

#include "graphics/Texture.h"

class MediaItem;

class AdaptiveBackgroundTexture : public Texture {
  public:
    AdaptiveBackgroundTexture(MediaItem *item, int width, int height)
        : mItem(item), mDestWidth(width), mDestHeight(height) {}

    bool isCached() const override {
        return true;
    }

    bool shouldQueue() const override {
        return true;
    }

    bool loadsOverNetwork() const override;

    Bitmap load(RenderView *view) override;
    void startLoad(RenderView *view, const TexturePtr &self) override;

    // Builds the blur without a decoder or GL context.
    static Bitmap backdropFrom(const Bitmap &photo, int destWidth, int destHeight);

  private:
    // Source item for decodeItemPixels; uploaded textures no longer retain their bitmap.
    MediaItem *mItem;
    int mDestWidth;
    int mDestHeight;
};
