#include "GridCamera.h"

#include <cmath>

#include "FloatUtils.h"

static const float kPi = 3.14159265358979323846f;

GridCamera::GridCamera(int width, int height, int itemWidth, int itemHeight) {
    reset();
    viewportChanged(width, height, (float)itemWidth, (float)itemHeight);
    mConvergenceSpeed = 1.0f;
    mFriction = 0.0f;
}

void GridCamera::reset() {
    mTargetEyeX = 0.0f;
    mEyeX = EYE_X;
    mEyeY = EYE_Y;
    mEyeZ = EYE_Z;
    mLookAtX = EYE_X;
    mLookAtY = EYE_Y;
    mLookAtZ = 0.0f;
    mUpX = 0.0f;
    mUpY = 1.0f;
    mUpZ = 0.0f;
    mPosX = 0.0f;
    mPosY = 0.0f;
    mPosZ = 0.0f;
    mTargetPosX = 0.0f;
    mTargetPosY = 0.0f;
    mTargetPosZ = 0.0f;
}

void GridCamera::viewportChanged(int w, int h, float itemWidth, float itemHeight) {
    // For pixel precision: fov = 2 atan(qFactor / (2 defaultZ)), where
    // qFactor = height / itemHeight.
    float qFactor = (float)h / itemHeight;
    float fov = 2.0f * (float)(std::atan2(qFactor / 2.0f, EYE_Z) * 180.0 / kPi);
    mWidth = w;
    mHeight = h;
    mWidthBy2 = w >> 1;
    mHeightBy2 = h >> 1;
    mAspectRatio = (h == 0) ? 1.0f : (float)w / (float)h;
    mDefaultAspectRatio = (w > h) ? DEFAULT_LANDSCAPE_ASPECT : DEFAULT_PORTRAIT_ASPECT;
    mTanFovBy2 = std::tan(fov * 0.5f * (kPi / 180.0f));
    mItemHeight = (int)itemHeight;
    mItemWidth = (int)itemWidth;
    mScale = itemHeight;
    mOneByScale = 1.0f / itemHeight;
    mFov = fov;
}

void GridCamera::convertToCameraSpace(float posX, float posY, float posZ, Vector3f &retVal) {
    float posXx = posX - (float)mWidthBy2;
    float posYx = posY - (float)mHeightBy2;
    convertToRelativeCameraSpace(posXx, posYx, posZ, retVal);
    retVal.x += (EYE_X + mTargetPosX);
    retVal.y += mTargetPosY;
}

void GridCamera::convertToRelativeCameraSpace(float posX, float posY, float posZ, Vector3f &retVal) {
    float posXx = posX / (float)mWidth;
    float posYx = posY / (float)mHeight;
    float zDiscriminant = mTanFovBy2 * (mTargetPosZ + EYE_Z + posZ);
    zDiscriminant *= 2.0f;
    float yRange = zDiscriminant;
    float xRange = zDiscriminant * mAspectRatio;
    retVal.x = posXx * xRange;
    retVal.y = posYx * yRange;
}

float GridCamera::getDistanceToFitRect(float f, float g) {
    const float thisAspectRatio = f / g;
    float h = g;
    if (thisAspectRatio > mAspectRatio) {
        // The width will hit the screen first.
        h = (f * (float)mHeight) / (float)mWidth;
    }
    // Fitting mItemHeight pixels means targetZ 1.0f for the given fov.
    h = h / (float)mItemHeight;
    float targetZ = h / mTanFovBy2;
    targetZ = targetZ * 0.5f;
    return -(EYE_Z - targetZ);
}

void GridCamera::moveXTo(float posX) {
    mTargetPosX = posX;
}

void GridCamera::moveYTo(float posY) {
    mTargetPosY = posY;
}

void GridCamera::moveZTo(float posZ) {
    mTargetPosZ = posZ;
}

void GridCamera::moveTo(float posX, float posY, float posZ) {
    float delta = posX - mTargetPosX;
    float maxDelta = (float)mWidth * 2.0f * mOneByScale;
    delta = FloatUtils::clamp(delta, -maxDelta, maxDelta);
    mTargetPosX += delta;
    mTargetPosY = posY;
    mTargetPosZ = posZ;
}

void GridCamera::moveBy(float posX, float posY, float posZ) {
    moveTo(posX + mTargetPosX, posY + mTargetPosY, posZ + mTargetPosZ);
}

void GridCamera::commitMove() {
    mPosX = mTargetPosX;
    mPosY = mTargetPosY;
    mPosZ = mTargetPosZ;
}

void GridCamera::commitMoveInX() {
    mPosX = mTargetPosX;
}

void GridCamera::commitMoveInY() {
    mPosY = mTargetPosY;
}

void GridCamera::commitMoveInZ() {
    mPosZ = mTargetPosZ;
}

void GridCamera::scrollRange(const Vector3f &firstSlotPosition, const Vector3f &lastSlotPosition, float *minX,
                             float *maxX) {
    const float oneByItemHeight = (mItemHeight > 0) ? (1.0f / (float)mItemHeight) : 0.0f;
    *minX = firstSlotPosition.x * oneByItemHeight;
    *maxX = lastSlotPosition.x * oneByItemHeight;
    if (mItemHeight <= 0) {
        return;
    }

    // Centre walls narrower than the viewport by collapsing both scroll bounds to their
    // midpoint.
    Vector3f leftEdge;
    Vector3f rightEdge;
    convertToCameraSpace(0.0f, 0.0f, 0.0f, leftEdge);
    convertToCameraSpace((float)mWidth, 0.0f, 0.0f, rightEdge);
    const float wallWidth = (*maxX - *minX) + (float)mItemWidth * oneByItemHeight;
    if (wallWidth <= (rightEdge.x - leftEdge.x)) {
        const float middle = (*minX + *maxX) * 0.5f;
        *minX = middle;
        *maxX = middle;
    }
}

void GridCamera::clampToScrollRange(const Vector3f &firstSlotPosition, const Vector3f &lastSlotPosition) {
    float minX = 0.0f;
    float maxX = 0.0f;
    scrollRange(firstSlotPosition, lastSlotPosition, &minX, &maxX);
    if (mTargetPosX < minX) {
        moveXTo(minX);
    } else if (mTargetPosX > maxX) {
        moveXTo(maxX);
    }
}

bool GridCamera::computeConstraints(bool applyConstraints, bool applyOverflowFeedback,
                                    const Vector3f &firstSlotPosition, const Vector3f &lastSlotPosition) {
    bool retVal = false;
    float minX = 0.0f;
    float maxX = 0.0f;
    scrollRange(firstSlotPosition, lastSlotPosition, &minX, &maxX);
    if (mTargetPosX < minX) {
        mAmountExceeding += mTargetPosX - minX;
        mTargetPosX = minX;
        mPosX = minX;
        if (applyConstraints) {
            mTargetPosX = minX;
            mFriction = 0.0f;
        }
        retVal = true;
    }
    if (mTargetPosX > maxX) {
        mAmountExceeding += mTargetPosX - maxX;
        mTargetPosX = maxX;
        mPosX = maxX;
        if (applyConstraints) {
            mTargetPosX = maxX;
            mFriction = 0.0f;
        }
        retVal = true;
    }
    if (!retVal) {
        float scrollingFromEdgeX;
        if (mAmountExceeding < 0.0f) {
            scrollingFromEdgeX = mTargetPosX - minX;
        } else {
            scrollingFromEdgeX = maxX - mTargetPosX;
        }
        if (scrollingFromEdgeX > 0.1f) {
            mAmountExceeding = 0.0f;
        }
    }
    if (applyConstraints) {
        mEyeEdgeOffsetX = 0.0f;
        const float maxBounceBack = 0.8f;
        if (mAmountExceeding < -maxBounceBack) {
            mAmountExceeding = -maxBounceBack;
        }
        if (mAmountExceeding > maxBounceBack) {
            mAmountExceeding = maxBounceBack;
        }
        if (mTargetPosX > maxX) {
            mTargetPosX = maxX;
        }
        if (mTargetPosX < minX) {
            mTargetPosX = minX;
        }
        mAmountExceeding = 0.0f;
    } else {
        float amountExceedingToUse = mAmountExceeding;
        const float maxThreshold = 0.6f;
        if (amountExceedingToUse > maxThreshold) {
            amountExceedingToUse = maxThreshold;
        }
        if (amountExceedingToUse < -maxThreshold) {
            amountExceedingToUse = -maxThreshold;
        }
        // Half the original's 10. One wheel tick asks for a fifth of the
        // window, which is far enough past the end to peg the clamp above on
        // the first tick, so the wall took its full lean at once rather than
        // easing into one the way a drag does.
        const float leanScale = 5.0f;
        mEyeEdgeOffsetX = applyOverflowFeedback ? (-leanScale * amountExceedingToUse) : 0.0f;
    }
    return retVal;
}

void GridCamera::stopMovement() {
    mTargetPosX = mPosX;
    mTargetPosY = mPosY;
    mTargetPosZ = mPosZ;
}

void GridCamera::stopMovementInX() {
    mTargetPosX = mPosX;
}

void GridCamera::stopMovementInY() {
    mTargetPosY = mPosY;
}

void GridCamera::stopMovementInZ() {
    mTargetPosZ = mPosZ;
}

bool GridCamera::isAnimating() const {
    return mPosX != mTargetPosX || mPosY != mTargetPosY || mPosZ != mTargetPosZ || mEyeOffsetAnimX != mEyeOffsetX ||
           mEyeEdgeOffsetXAnim != mEyeEdgeOffsetX;
}

bool GridCamera::isZAnimating() const {
    return mPosZ != mTargetPosZ;
}

float GridCamera::worldUnitsPerPixel(float z) const {
    if (mHeight <= 0) {
        return 0.0f;
    }
    return 2.0f * mTanFovBy2 * (mEyeZ + z) / (float)mHeight;
}

void GridCamera::update(float timeElapsed) {
    timeElapsed = timeElapsed * mConvergenceSpeed;
    float oldPosX = mPosX;
    mPosX = FloatUtils::animate(mPosX, mTargetPosX, timeElapsed);
    float diff = mPosX - oldPosX;
    if (diff == 0.0f) {
        mFriction = 0.0f;
    }
    mTargetPosX += (diff * mFriction);
    mPosY = FloatUtils::animate(mPosY, mTargetPosY, timeElapsed);
    mPosZ = FloatUtils::animate(mPosZ, mTargetPosZ, timeElapsed);
    if (mEyeZ != EYE_Z) {
        mEyeOffsetX = 0.0f;
        mEyeOffsetY = 0.0f;
    }
    mEyeOffsetAnimX = FloatUtils::animate(mEyeOffsetAnimX, mEyeOffsetX, timeElapsed);
    mEyeOffsetAnimY = FloatUtils::animate(mEyeOffsetAnimY, mEyeOffsetY, timeElapsed);
    mEyeEdgeOffsetXAnim = FloatUtils::animate(mEyeEdgeOffsetXAnim, mEyeEdgeOffsetX, timeElapsed);
    mTargetEyeX = EYE_X + mPosX;
    mEyeX = mTargetEyeX;
    mEyeX += (mEyeOffsetAnimX + mEyeEdgeOffsetXAnim);
    mLookAtX = EYE_X + mPosX;
    mEyeY = EYE_Y + mPosY;
    mLookAtY = EYE_Y + mPosY;
    mEyeZ = EYE_Z + mPosZ;
    mLookAtZ = mPosZ;
}
