#include "grid/DisplayItem.h"

#include <cmath>
#include <cstdlib>
#include <random>

#include "app/App.h"
#include "core/FloatUtils.h"
#include "grid/GridLayer.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "core/Shared.h"

namespace {

float nextRandom() {
    static thread_local std::mt19937 generator(12345);
    static thread_local std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    return distribution(generator);
}

}  // namespace

DisplayItem::DisplayItem(MediaItem *item) : mItemRef(item) {
    mAnimatedImageTheta = item ? item->mRotation : 0.0f;
    mImageTheta = mAnimatedImageTheta;
    mCurrentSlotIndex = Shared::INVALID;
}

void DisplayItem::rotateImageBy(float theta) {
    mImageTheta += theta;
}

void DisplayItem::set(const Vector3f &position, int stackIndex, bool performTransition) {
    (void)performTransition;
    mConvergenceSpeed = 1.0f;
    int seed = stackIndex;
    int randomSeed = stackIndex;

    if (seed > 3) {
        seed = 3;
        randomSeed = 0;
    }

    if (!mAlive) {
        mAnimatedPosition.set(position);
        mAnimatedPosition.z = -3.0f + stackIndex * STACK_SPACING;
    }

    mTargetPosition.set(position);
    if (mStackId != stackIndex && stackIndex >= 0) {
        mStackId = stackIndex;
    }

    if (randomSeed == 0) {
        if (stackIndex == 0) {
            mTargetTheta = 0.0f;
        } else if (mTargetTheta == 0.0f) {
            mTargetTheta = 30.0f * (0.5f - nextRandom());
        }
        mTargetPosition.z = seed * STACK_SPACING;
        mJitteredPosition.set(0.0f, 0.0f, seed * STACK_SPACING);
    } else {
        int sign = (seed % 2 == 0) ? 1 : -1;
        if (seed != 0 && !mStacktopPosition.equals(position) && mTargetTheta == 0.0f) {
            mTargetTheta = 30.0f * (0.5f - nextRandom());
            mJitteredPosition.x = sign * 12.0f * seed + (0.5f - nextRandom()) * 4 * seed;
            mJitteredPosition.y = sign * 4.0f + ((sign == 1) ? -8.0f : sign * nextRandom() * 16.0f);
            mJitteredPosition.x *= App::PIXEL_DENSITY;
            mJitteredPosition.y *= App::PIXEL_DENSITY;
            mJitteredPosition.z = seed * STACK_SPACING;
        }
    }
    mTargetPosition.add(mJitteredPosition);
    mStacktopPosition.set(position);
    mStartOffset = 0.0f;
}

TexturePtr DisplayItem::getThumbnailImage(const MediaItemTexture::Config *config) {
    if (!mThumbnailImage && config != nullptr) {
        if (mItemRef && mItemRef->mId != Shared::INVALID) {
            mThumbnailImage = std::make_shared<MediaItemTexture>(config, mItemRef);
        }
    }
    return mThumbnailImage;
}

TexturePtr DisplayItem::getScreennailImage() {
    if (!mScreennailImage || mScreennailImage->mState == Texture::STATE_ERROR) {
        if (mItemRef && !mItemRef->mScreennailUri.empty()) {
            mScreennailImage = std::make_shared<MediaItemTexture>(nullptr, mItemRef);
        }
    }
    return mScreennailImage;
}

TexturePtr DisplayItem::getHiResImage() {
    if (!mHiResImage && mItemRef) {
        mHiResImage = std::make_shared<FileTexture>(mItemRef->mContentUri, App::HI_RES_MAX_EDGE, mItemRef);
    }
    return mHiResImage;
}

TiledImage *DisplayItem::getTiledImage() {
    if (!TiledImage::canTile(mItemRef)) {
        return nullptr;
    }
    if (!mTiledImage) {
        mTiledImage = std::make_unique<TiledImage>(mItemRef);
    }
    return mTiledImage.get();
}

void DisplayItem::clearScreennailImage() {
    if (mScreennailImage) {
        mScreennailImage.reset();
        mHiResImage.reset();
    }
    mTiledImage.reset();
}

void DisplayItem::clearTiledImage() {
    mTiledImage.reset();
}

void DisplayItem::clearHiResImage() {
    mHiResImage.reset();
}

void DisplayItem::clearThumbnail() {
    mThumbnailImage.reset();
}

bool DisplayItem::isAnimating() const {
    return mAlive && (mPerformingScale || !mAnimatedPosition.equals(mTargetPosition) ||
                      mAnimatedTheta != mTargetTheta || mAnimatedImageTheta != mImageTheta ||
                      mAnimatedPlaceholderFade != 1.0f);
}

void DisplayItem::update(float timeElapsedInSec) {
    if (!mAlive) {
        return;
    }
    timeElapsedInSec *= 1.25f;
    timeElapsedInSec *= mConvergenceSpeed;
    mAnimatedPosition.x = FloatUtils::animate(mAnimatedPosition.x, mTargetPosition.x, timeElapsedInSec);
    mAnimatedPosition.y = FloatUtils::animate(mAnimatedPosition.y, mTargetPosition.y, timeElapsedInSec);
    mAnimatedTheta = FloatUtils::animate(mAnimatedTheta, mTargetTheta, timeElapsedInSec);
    mAnimatedImageTheta = FloatUtils::animate(mAnimatedImageTheta, mImageTheta, timeElapsedInSec);
    mAnimatedPlaceholderFade = FloatUtils::animate(mAnimatedPlaceholderFade, 1.0f, timeElapsedInSec);
    mAnimatedPosition.z = FloatUtils::animate(mAnimatedPosition.z, mTargetPosition.z, timeElapsedInSec);
}

void DisplayItem::commit() {
    mAnimatedPosition.set(mTargetPosition);
    mAnimatedTheta = mTargetTheta;
    mAnimatedImageTheta = mImageTheta;
}

void DisplayItem::setHovered(bool hovered, bool pushDown) {
    if (!hovered && !mHovered) {
        return;
    }
    mHovered = hovered;
    mConvergenceSpeed = 2.0f;
    int seed = mStackId;
    if (seed > 3) {
        seed = 3;
    }
    mTargetPosition.set(mStacktopPosition);
    mTargetPosition.add(mJitteredPosition);
    mTargetPosition.z = seed * STACK_SPACING;
    if (!hovered) {
        return;
    }
    // Focus, which a press shows, doubles a stack's jitter to spread it and
    // brings a photo on the grid half a unit forward. A hover goes this much
    // of the way.
    const float fraction = 0.4f;
    if (pushDown) {
        mTargetPosition.add(mJitteredPosition.x * fraction, mJitteredPosition.y * fraction, 0.0f);
    } else {
        mTargetPosition.z -= 0.5f * fraction;
    }
}

void DisplayItem::setHasFocus(bool hasFocus, bool pushDown) {
    mConvergenceSpeed = 2.0f;
    mHasFocus = hasFocus;
    // Focus takes the place of a hover. setHovered puts the hover back once
    // focus has gone, if the mouse is still there.
    mHovered = false;
    int seed = mStackId;
    if (seed > 3) {
        seed = 3;
    }
    if (hasFocus) {
        mTargetPosition.set(mStacktopPosition);
        mTargetPosition.add(mJitteredPosition);
        mTargetPosition.add(mJitteredPosition);
        mTargetPosition.z = seed * STACK_SPACING + (pushDown ? 1.0f : -0.5f);
    } else {
        mTargetPosition.set(mStacktopPosition);
        mTargetPosition.add(mJitteredPosition);
        mTargetPosition.z = seed * STACK_SPACING;
    }
}

void DisplayItem::setSingleOffset(bool useOffset, bool pushAway, float x, float y, float z, float spreadValue) {
    (void)x;
    (void)y;
    (void)z;
    int seed = mStackId;
    if (useOffset) {
        mTargetPosition.set(mStacktopPosition);
        if (spreadValue > 4.0f) {
            spreadValue = 4.0f + 0.1f * spreadValue;
        }
        if (spreadValue < 1.0f) {
            spreadValue = 1.0f / spreadValue;
            pushAway = true;
        }
        if (!pushAway) {
            if (seed == 0) {
                mTargetPosition.add(0.0f, -spreadValue * 14.0f, 0.0f);
            }
            if (seed == 1) {
                mTargetPosition.add(-spreadValue * 32.0f, 0.0f, 0.0f);
            }
            if (seed == 2) {
                mTargetPosition.add(0.0f, spreadValue * 14.0f, 0.0f);
            }
            if (seed == 3) {
                mTargetPosition.add(spreadValue * 32.0f, 0.0f, 0.0f);
            }
            mTargetPosition.z = -1.0f * spreadValue + seed * STACK_SPACING * spreadValue;
            mTargetTheta = 0.0f;
        } else {
            mTargetPosition.z = seed * STACK_SPACING + spreadValue * 0.5f;
        }
    } else {
        if (seed > 3) {
            seed = 3;
        }
        mTargetPosition.set(mStacktopPosition);
        mTargetPosition.add(mJitteredPosition);
        mTargetPosition.z = seed * STACK_SPACING;
        if (seed != 0 && mTargetTheta == 0.0f) {
            mTargetTheta = 30.0f * (0.5f - nextRandom());
        }
        mStartOffset = 0.0f;
    }
}

void DisplayItem::setOffset(bool useOffset, bool pushDown, float span, float dx1, float dy1, float dx2, float dy2) {
    int seed = mStackId;
    if (useOffset) {
        mPerformingScale = true;
        float spanDelta = span - mSpan;
        float maxSlots = mItemRef && mItemRef->mParentMediaSet
                             ? (float)mItemRef->mParentMediaSet->getNumExpectedItems()
                             : 0.0f;
        maxSlots = FloatUtils::clamp(maxSlots, 0.0f, (float)GridLayer::MAX_ITEMS_PER_SLOT);
        if (std::fabs(spanDelta) < 10.0f * App::PIXEL_DENSITY) {
            mStartOffset += (mSpanDirection * mSpanSpeed);
            mStartOffset = FloatUtils::clamp(mStartOffset, 0.0f, maxSlots);
        } else {
            mSpanSpeed = std::fabs(span / (600.0f * App::PIXEL_DENSITY));
            if (mSpanSpeed > 2.0f) {
                mSpanSpeed = 2.0f;
            }
            mSpanSpeed *= 0.1f;
            mSpanDirection = (spanDelta > 0.0f) ? 1.0f : ((spanDelta < 0.0f) ? -1.0f : 0.0f);
        }
        mSpan = span;
        mTargetPosition.set(mStacktopPosition);
        if (!pushDown) {
            if (maxSlots < 2.0f) {
                return;
            }
            // The stack top tracks the top finger, the rest fan out toward the
            // bottom one, weighted by how far the fingers have spread.
            int maxSeedVal = (int)(span / (125.0f * App::PIXEL_DENSITY));
            maxSeedVal = FloatUtils::clamp(maxSeedVal, 2, (int)maxSlots - 1);
            float startOffset = FloatUtils::clamp(mStartOffset, 0.0f, maxSlots - (float)maxSeedVal - 1.0f);
            float offsetSeed = (float)seed - startOffset;
            float seedFactor = offsetSeed / (float)maxSeedVal;
            seedFactor = FloatUtils::clamp(seedFactor, 0.0f, 1.0f);
            float dx = dx2 * seedFactor + (1.0f - seedFactor) * dx1;
            float dy = dy2 * seedFactor + (1.0f - seedFactor) * dy1;
            mTargetPosition.add(dx, dy, seed * 0.1f);
            mTargetTheta = 0.0f;
        } else {
            mStartOffset = 0.0f;
            mTargetPosition.z = seed * STACK_SPACING + 3.0f;
        }
    } else {
        mPerformingScale = false;
        mStartOffset = 0.0f;
        if (seed > 3) {
            seed = 3;
        }
        mTargetPosition.set(mStacktopPosition);
        mTargetPosition.add(mJitteredPosition);
        mTargetPosition.z = seed * STACK_SPACING;
        if (seed != 0 && mTargetTheta == 0.0f) {
            mTargetTheta = 30.0f * (0.5f - nextRandom());
        }
    }
}
