// Port of com.cooliris.media.CrossFadingTexture.
//
// Holds the texture being shown and the one it replaced, and walks a mix ratio
// from the old to the new. The original blended the pair with a GL_INTERPOLATE
// texture combiner; here RenderView::bindMixed runs the two texture program.
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
