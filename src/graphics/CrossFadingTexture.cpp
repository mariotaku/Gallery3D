#include "graphics/CrossFadingTexture.h"

#include "core/FloatUtils.h"
#include "graphics/RenderView.h"

CrossFadingTexture::CrossFadingTexture(TexturePtr initialTexture)
    : mTexture(initialTexture), mFadingTexture(std::move(initialTexture)), mMixRatio(1.0f),
      mAnimatedMixRatio(1.0f) {}

void CrossFadingTexture::clear() {
    mTexture.reset();
    mFadingTexture.reset();
}

void CrossFadingTexture::setTexture(const TexturePtr &texture) {
    if (mTexture == texture || !texture || mAnimatedMixRatio < 1.0f) {
        return;
    }
    // Nothing to fade from on the first texture, so fade it in on alpha.
    mFadeNecessary = !mFadingTexture;
    mFadingTexture = mTexture ? mTexture : texture;
    mTexture = texture;
    mAnimatedMixRatio = 0.0f;
    mMixRatio = 1.0f;
}

void CrossFadingTexture::setTextureImmediate(const TexturePtr &texture) {
    if (!texture || !texture->isLoaded() || mTexture == texture) {
        return;
    }
    if (mTexture) {
        mFadingTexture = mTexture;
    }
    mTexture = texture;
    mMixRatio = 1.0f;
}

bool CrossFadingTexture::update(float timeElapsed) {
    if (mTexture && mFadingTexture && mTexture->isLoaded() && mFadingTexture->isLoaded()) {
        mAnimatedMixRatio = FloatUtils::animate(mAnimatedMixRatio, mMixRatio, timeElapsed * 0.5f);
        return mMixRatio != mAnimatedMixRatio;
    }
    mAnimatedMixRatio = 0.0f;
    return false;
}

bool CrossFadingTexture::bind(RenderView *view) {
    if (mBind) {
        return true;  // Already bound.
    }
    if (mFadingTexture && mFadingTexture->mState == Texture::STATE_ERROR) {
        mFadingTexture.reset();
    }
    if (mTexture && mTexture->mState == Texture::STATE_ERROR) {
        mTexture.reset();
    }
    mBindUsingMixed = false;
    bool fadingTextureLoaded = false;
    bool textureLoaded = false;
    if (mFadingTexture) {
        fadingTextureLoaded = view->bind(mFadingTexture);
    }
    if (mTexture) {
        view->bind(mTexture);
        textureLoaded = mTexture->isLoaded();
    }
    if (mFadeNecessary) {
        if (view->getAlpha() > mAnimatedMixRatio) {
            view->setAlpha(mAnimatedMixRatio);
        }
        if (mAnimatedMixRatio == 1.0f) {
            mFadeNecessary = false;
        }
    }
    if (!textureLoaded && !fadingTextureLoaded) {
        return false;
    }
    mBind = true;
    if (mAnimatedMixRatio <= 0.0f && fadingTextureLoaded) {
        view->bind(mFadingTexture);
    } else if (mAnimatedMixRatio >= 1.0f || !fadingTextureLoaded || view->getAlpha() < mAnimatedMixRatio ||
               mFadingTexture == mTexture) {
        view->bind(mTexture);
    } else {
        mBindUsingMixed = true;
        view->bindMixed(mFadingTexture, mTexture, mAnimatedMixRatio);
    }
    return true;
}

void CrossFadingTexture::unbind(RenderView *view) {
    if (mBindUsingMixed && mBind) {
        view->unbindMixed();
        mBindUsingMixed = false;
    }
    mBind = false;
}
