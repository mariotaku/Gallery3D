// Port of com.cooliris.media.GridInputProcessor.
// SDL accelerometer input is enabled only on devices with a sensor.
#pragma once

#include "DisplayItem.h"
#include "GestureDetector.h"
#include "GridCamera.h"
#include "IndexRange.h"
#include "Input.h"
#include "Vector3f.h"

class GridLayer;
class RenderView;

class GridInputProcessor : public GestureDetector::Listener, public ScaleGestureDetector::Listener {
  public:
    GridInputProcessor(GridCamera *camera, GridLayer *layer, RenderView *view, DisplayItem **displayItems);

    // Leans the wall from the accelerometer. x, y and z are metres per second
    // squared in the display's orientation, already rotated by the caller.
    // `state` is the GridLayer state, since fullscreen does not lean.
    void onSensorChanged(RenderView *view, float x, float y, float z, int state);


    int getCurrentFocusSlot() const {
        return mCurrentFocusSlot;
    }

    int getCurrentSelectedSlot() const {
        return mCurrentSelectedSlot;
    }

    int getCurrentScaledSlot() const {
        return mCurrentScaleSlot;
    }

    void setCurrentSelectedSlot(int slot);
    void setCurrentFocusSlot(int slotId) {
        mCurrentSelectedSlot = slotId;
    }

    bool onTouchEvent(const MotionEvent &event);
    bool onKeyDown(int keyCode, const KeyEvent &event, int state);
    void onWheel(float focusX, float focusY, float ticks);

    void clearSelection() {
        mCurrentSelectedSlot = -1;
    }

    void clearFocus() {
        mCurrentFocusSlot = -1;
    }

    bool isFocusItemPressed() const {
        return mCurrentFocusIsPressed;
    }

    void update(float timeElapsed);

    float getScale() const {
        return mScale;
    }

    void resetScale();

    ScaleGestureDetector *getScaleGestureDetector() {
        return &mScaleGestureDetector;
    }

    bool touchPressed() const {
        return mProcessTouch;
    }

    // GestureDetector::Listener
    bool onDown(const MotionEvent &event) override;
    bool onFling(const MotionEvent &down, const MotionEvent &up, float velocityX, float velocityY) override;
    void onLongPress(const MotionEvent &event) override;
    bool onScroll(const MotionEvent &down, const MotionEvent &move, float distanceX, float distanceY) override;
    void onShowPress(const MotionEvent &event) override;
    bool onSingleTapUp(const MotionEvent &event) override;
    bool onDoubleTap(const MotionEvent &event) override;
    bool onDoubleTapEvent(const MotionEvent &event) override;
    bool onSingleTapConfirmed(const MotionEvent &event) override;

    // ScaleGestureDetector::Listener
    bool onScale(ScaleGestureDetector *detector) override;
    bool onScaleBegin(ScaleGestureDetector *detector) override;
    void onScaleEnd(ScaleGestureDetector *detector, bool cancel) override;

  private:
    void touchBegan(int posX, int posY);
    void touchMoved(int posX, int posY, float timeElapsed);
    void touchEnded(int posX, int posY, float timeElapsed);
    void constrainCamera(bool b);
    void selectSlot(int slotId);

    int mCurrentFocusSlot = -1;
    bool mCurrentFocusIsPressed = false;
    int mCurrentSelectedSlot = -1;

    int mTouchPosX = 0;
    int mTouchPosY = 0;
    int mActionCode = 0;
    uint64_t mPrevTouchTime = 0;
    float mFirstTouchPosX = 0.0f;
    float mFirstTouchPosY = 0.0f;
    float mPrevTouchPosX = 0.0f;
    float mPrevTouchPosY = 0.0f;
    float mTouchVelX = 0.0f;
    float mTouchVelY = 0.0f;
    bool mProcessTouch = false;
    bool mTouchMoved = false;
    float mDpadIgnoreTime = 0.0f;

    GridCamera *mCamera;
    GridLayer *mLayer;
    RenderView *mView;
    DisplayItem **mDisplayItems;

    bool mPrevHitEdge = false;
    bool mTouchFeedbackDelivered = false;
    GestureDetector mGestureDetector;
    ScaleGestureDetector mScaleGestureDetector;
    bool mZoomGesture = false;

    // Never assigned, matching the Java tilt response; see onSensorChanged.
    float mPrevTiltValueLowPass = 0.0f;
    int mCurrentScaleSlot = -1;
    float mScale = 1.0f;
};
