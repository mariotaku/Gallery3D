// Port of com.cooliris.media.CrossFadingTexture.
// Fades between two textures using RenderView::bindMixed.
#pragma once

#include "Texture.h"

class RenderView;

class CrossFadingTexture {
  public:
    CrossFadingTexture() = default;
    explicit CrossFadingTexture(TexturePtr initialTexture);

    void clear();

    const TexturePtr &getTexture() const {
        return mTexture;
    }

    void setTexture(const TexturePtr &texture);
    void setTextureImmediate(const TexturePtr &texture);

    // Returns true while the fade is still moving.
    bool update(float timeElapsed);

    bool bind(RenderView *view);
    void unbind(RenderView *view);

  private:
    TexturePtr mTexture;
    TexturePtr mFadingTexture;
    float mMixRatio = 0.0f;
    float mAnimatedMixRatio = 0.0f;
    bool mBindUsingMixed = false;
    bool mBind = false;
    bool mFadeNecessary = false;
};
