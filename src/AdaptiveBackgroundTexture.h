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
    // The item rather than the thumbnail texture, because a Texture cannot be
    // asked for its pixels at all: load() runs on a loader thread and has to
    // return a bitmap there and then, which a browser decode cannot do, so the
    // work lives in startLoad and load() returns nothing. decodeItemPixels is
    // the way in.
    MediaItem *mItem;
    int mDestWidth;
    int mDestHeight;
};
