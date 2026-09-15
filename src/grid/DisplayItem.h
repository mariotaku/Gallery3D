// Port of com.cooliris.media.DisplayItem.
#pragma once

#include <memory>

#include "graphics/Texture.h"
#include "graphics/TiledImage.h"
#include "core/Vector3f.h"

class MediaItem;

class DisplayItem {
  public:
    static constexpr float STACK_SPACING = 0.2f;

    explicit DisplayItem(MediaItem *item);

    // Takes the item's late details, such as a rotation its source read only
    // just before the thumbnail loaded. Called on the render thread each frame
    // the item is laid out.
    void takeLateDetails();

    void rotateImageBy(float theta);
    void set(const Vector3f &position, int stackIndex, bool performTransition);

    int getStackIndex() const {
        return mStackId;
    }

    // The grid thumbnail, made on first use. halfSize asks for the one a card
    // behind the top of a stack takes, and a full one already made serves it
    // as well. Without a config, whichever has been made, full first.
    TexturePtr getThumbnailImage(const MediaItemTexture::Config *config, bool halfSize = false);
    // The small thumbnail, while it is loaded and the full one is not yet.
    TexturePtr getStandInThumbnail() const;
    TexturePtr getScreennailImage();
    TexturePtr getHiResImage();
    // Tiles for fullscreen zoom, or null when TiledImage::canTile is false.
    TiledImage *getTiledImage();

    void clearScreennailImage();
    void clearHiResImage();
    void clearTiledImage();
    void clearThumbnail();

    bool isAnimating() const;
    void update(float timeElapsedInSec);
    void commit();

    void setHasFocus(bool hasFocus, bool pushDown);
    // Under a mouse that is only passing over: a fraction of what focus does.
    // Taking the hover away puts the item back where it rests.
    void setHovered(bool hovered, bool pushDown);
    void setSingleOffset(bool useOffset, bool pushAway, float x, float y, float z, float spreadValue);
    void setOffset(bool useOffset, bool pushDown, float span, float dx1, float dy1, float dx2, float dy2);

    bool getHasFocus() const {
        return mHasFocus;
    }

    bool isHovered() const {
        return mHovered;
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
    bool mHovered = false;
    Vector3f mTargetPosition;
    float mTargetTheta = 0.0f;
    float mImageTheta = 0.0f;
    int mStackId = 0;
    std::shared_ptr<MediaItemTexture> mThumbnailImage;
    std::shared_ptr<MediaItemTexture> mSmallThumbnailImage;
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
