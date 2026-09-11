// Port of com.cooliris.media.DisplayItem.
#pragma once

#include <memory>

#include "Texture.h"
#include "TiledImage.h"
#include "Vector3f.h"

class MediaItem;

class DisplayItem {
  public:
    static constexpr float STACK_SPACING = 0.2f;

    explicit DisplayItem(MediaItem *item);

    void rotateImageBy(float theta);
    void set(const Vector3f &position, int stackIndex, bool performTransition);

    int getStackIndex() const {
        return mStackId;
    }

    TexturePtr getThumbnailImage(const MediaItemTexture::Config *config);
    TexturePtr getScreennailImage();
    TexturePtr getHiResImage();
    // The picture as a grid of pieces, for the zoomed fullscreen view. Null
    // where the item cannot be drawn that way - see TiledImage::canTile - and
    // the screennail is then all there is.
    TiledImage *getTiledImage();

    void clearScreennailImage();
    void clearHiResImage();
    void clearTiledImage();
    void clearThumbnail();

    bool isAnimating() const;
    void update(float timeElapsedInSec);
    void commit();

    void setHasFocus(bool hasFocus, bool pushDown);
    void setSingleOffset(bool useOffset, bool pushAway, float x, float y, float z, float spreadValue);
    void setOffset(bool useOffset, bool pushDown, float span, float dx1, float dy1, float dx2, float dy2);

    bool getHasFocus() const {
        return mHasFocus;
    }

    bool isAlive() const {
        return mAlive;
    }

    float getImageTheta() const {
        return mImageTheta;
    }

    MediaItem *const mItemRef;
    float mAnimatedTheta = 0.0f;
    float mAnimatedImageTheta = 0.0f;
    float mAnimatedPlaceholderFade = 0.0f;
    bool mAlive = false;
    Vector3f mAnimatedPosition;
    int mCurrentSlotIndex = -1;
    // Set by DisplayList so it can drop the item from the animating list.
    bool mInAnimatables = false;

  private:
    Vector3f mStacktopPosition{-1.0f, -1.0f, -1.0f};
    Vector3f mJitteredPosition;
    bool mHasFocus = false;
    Vector3f mTargetPosition;
    float mTargetTheta = 0.0f;
    float mImageTheta = 0.0f;
    int mStackId = 0;
    std::shared_ptr<MediaItemTexture> mThumbnailImage;
    TexturePtr mScreennailImage;
    TexturePtr mHiResImage;
    std::unique_ptr<TiledImage> mTiledImage;
    float mConvergenceSpeed = 1.0f;

    bool mPerformingScale = false;
    float mSpan = 0.0f;
    float mSpanDirection = 0.0f;
    float mStartOffset = 0.0f;
    float mSpanSpeed = 0.0f;
};
