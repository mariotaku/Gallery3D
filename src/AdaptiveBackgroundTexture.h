// Port of com.cooliris.media.AdaptiveBackgroundTexture.
//
// Builds the backdrop out of a photo: downsample the thumbnail, box blur it
// twice and darken it, so the wall sits on a wash of its own colours. The right
// edge fades out because BackgroundLayer stitches the result with a 25 percent
// overlap and needs the tiles to blend into each other.
#pragma once

#include "Texture.h"

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

    // The blur itself, given the photo to build it from. Separate from the
    // loading so it can be checked without a decoder or a GL context.
    static Bitmap backdropFrom(const Bitmap &photo, int destWidth, int destHeight);

  private:
    // The photo this backdrop is derived from, decoded again rather than read
    // back from GL: the render view drops the bitmap after upload, and a
    // texture's own pixels are not reachable once they are on the card.
    //
    // The item, not the thumbnail texture. Asking the texture to load itself a
    // second time used to work and does not any more - a decode can answer
    // later than the call that started it, so a texture's load() returns
    // nothing and startLoad does the work. That left this reading an empty
    // bitmap and the wall sitting on its fallback gradient for good.
    MediaItem *mItem;
    int mDestWidth;
    int mDestHeight;
};
