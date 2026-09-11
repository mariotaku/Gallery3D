// Port of com.cooliris.media.GridCamera.
#pragma once

#include "Vector3f.h"

class GridCamera {
  public:
    static constexpr float MAX_CAMERA_SPEED = 12.0f;
    static constexpr float EYE_CONVERGENCE_SPEED = 3.0f;
    static constexpr float EYE_X = 0.0f;
    static constexpr float EYE_Y = 0.0f;
    static constexpr float EYE_Z = 8.0f;  // Initial z distance.

    float mEyeX = 0.0f;
    float mEyeY = 0.0f;
    float mEyeZ = 0.0f;
    float mLookAtX = 0.0f;
    float mLookAtY = 0.0f;
    float mLookAtZ = 0.0f;
    float mUpX = 0.0f;
    float mUpY = 0.0f;
    float mUpZ = 0.0f;

    // Tilts the wall.
    float mEyeOffsetX = 0.0f;
    float mEyeOffsetY = 0.0f;

    // Animation speed, 1.0f is normal speed.
    float mConvergenceSpeed = 1.0f;

    float mFov = 45.0f;
    float mScale = 1.0f;
    float mOneByScale = 1.0f;
    int mWidth = 0;
    int mHeight = 0;
    int mItemHeight = 0;
    int mItemWidth = 0;
    float mAspectRatio = 1.0f;
    float mDefaultAspectRatio = 1.0f;
    float mFriction = 0.0f;

    GridCamera(int width, int height, int itemWidth, int itemHeight);

    void reset();
    void viewportChanged(int w, int h, float itemWidth, float itemHeight);

    void convertToCameraSpace(float posX, float posY, float posZ, Vector3f &retVal);
    void convertToRelativeCameraSpace(float posX, float posY, float posZ, Vector3f &retVal);
    float getDistanceToFitRect(float f, float g);

    void moveXTo(float posX);
    void moveYTo(float posY);
    void moveZTo(float posZ);
    void moveTo(float posX, float posY, float posZ);
    void moveBy(float posX, float posY, float posZ);

    void commitMove();
    void commitMoveInX();
    void commitMoveInY();
    void commitMoveInZ();

    bool computeConstraints(bool applyConstraints, bool applyOverflowFeedback, const Vector3f &firstSlotPosition,
                            const Vector3f &lastSlotPosition);

    // Pulls the camera inside the scroll range. Unlike computeConstraints this
    // carries no bounce feedback, so it can run every frame: it is what keeps a
    // wall that fits parked in the middle when nothing is being dragged.
    void clampToScrollRange(const Vector3f &firstSlotPosition, const Vector3f &lastSlotPosition);

    void stopMovement();
    void stopMovementInX();
    void stopMovementInY();
    void stopMovementInZ();

    bool isAnimating() const;
    bool isZAnimating() const;

    void update(float timeElapsed);

  private:
    // Where the camera may look, between the first and last slot. A wall
    // narrower than the window collapses this to a point in the middle of it.
    void scrollRange(const Vector3f &firstSlotPosition, const Vector3f &lastSlotPosition, float *minX, float *maxX);

    static constexpr float DEFAULT_PORTRAIT_ASPECT = 320.0f / 480.0f;
    static constexpr float DEFAULT_LANDSCAPE_ASPECT = 480.0f / 320.0f;

    float mEyeEdgeOffsetX = 0.0f;
    float mEyeEdgeOffsetXAnim = 0.0f;
    float mAmountExceeding = 0.0f;

    float mPosX = 0.0f;
    float mPosY = 0.0f;
    float mPosZ = 0.0f;
    float mTargetPosX = 0.0f;
    float mTargetPosY = 0.0f;
    float mTargetPosZ = 0.0f;
    float mEyeOffsetAnimX = 0.0f;
    float mEyeOffsetAnimY = 0.0f;
    float mTargetEyeX = 0.0f;

    int mWidthBy2 = 0;
    int mHeightBy2 = 0;
    float mTanFovBy2 = 0.0f;
};
