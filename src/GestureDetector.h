// Ports of android.view.GestureDetector and
// com.cooliris.media.ScaleGestureDetector, cut down to what the grid uses.
#pragma once

#include <cstdint>

#include "Input.h"

class GestureDetector {
  public:
    class Listener {
      public:
        virtual ~Listener() = default;
        virtual bool onDown(const MotionEvent &event) = 0;
        virtual bool onFling(const MotionEvent &down, const MotionEvent &up, float velocityX, float velocityY) = 0;
        virtual void onLongPress(const MotionEvent &event) = 0;
        virtual bool onScroll(const MotionEvent &down, const MotionEvent &move, float distanceX, float distanceY) = 0;
        virtual void onShowPress(const MotionEvent &event) = 0;
        virtual bool onSingleTapUp(const MotionEvent &event) = 0;
        virtual bool onDoubleTap(const MotionEvent &event) = 0;
        virtual bool onDoubleTapEvent(const MotionEvent &event) = 0;
        virtual bool onSingleTapConfirmed(const MotionEvent &event) = 0;
    };

    explicit GestureDetector(Listener *listener) : mListener(listener) {}

    void setIsLongpressEnabled(bool enabled) {
        mLongpressEnabled = enabled;
    }

    bool onTouchEvent(const MotionEvent &event);
    // Long press fires from here, because there is no message loop to post to.
    void update(uint64_t nowMs);

  private:
    static const int TOUCH_SLOP = 16;
    static const int LONGPRESS_TIMEOUT_MS = 500;
    static const int DOUBLE_TAP_TIMEOUT_MS = 300;

    Listener *mListener;
    bool mLongpressEnabled = true;

    bool mDown = false;
    bool mMoved = false;
    bool mLongPressFired = false;
    MotionEvent mDownEvent;
    MotionEvent mLastEvent;
    uint64_t mDownTime = 0;
    uint64_t mPreviousUpTime = 0;
    float mPreviousUpX = 0.0f;
    float mPreviousUpY = 0.0f;
    float mVelocityX = 0.0f;
    float mVelocityY = 0.0f;
    uint64_t mLastMoveTime = 0;
};

class ScaleGestureDetector {
  public:
    class Listener {
      public:
        virtual ~Listener() = default;
        virtual bool onScale(ScaleGestureDetector *detector) = 0;
        virtual bool onScaleBegin(ScaleGestureDetector *detector) = 0;
        virtual void onScaleEnd(ScaleGestureDetector *detector, bool cancel) = 0;
    };

    explicit ScaleGestureDetector(Listener *listener) : mListener(listener) {}

    bool onTouchEvent(const MotionEvent &event);

    // Drives a scale gesture from a mouse wheel, so the grid can be spread and
    // photos zoomed without a touchscreen.
    void onWheel(float focusX, float focusY, float ticks);

    float getScaleFactor() const {
        return mScaleFactor;
    }

    float getCurrentSpan() const {
        return mCurrentSpan;
    }

    float getFocusX() const {
        return mFocusX;
    }

    float getFocusY() const {
        return mFocusY;
    }

    // Where the focus point sat on the previous move. Gingerbread added this so
    // a pinch can pan as well as zoom.
    float getPrevFocusX() const {
        return mPrevFocusX;
    }

    float getPrevFocusY() const {
        return mPrevFocusY;
    }

    float getTopFingerDeltaX() const {
        return mTopFingerDeltaX;
    }

    float getTopFingerDeltaY() const {
        return mTopFingerDeltaY;
    }

    float getBottomFingerDeltaX() const {
        return mBottomFingerDeltaX;
    }

    float getBottomFingerDeltaY() const {
        return mBottomFingerDeltaY;
    }

    bool isInProgress() const {
        return mInProgress;
    }

  private:
    void reset();

    Listener *mListener;
    bool mInProgress = false;
    float mScaleFactor = 1.0f;
    float mCurrentSpan = 0.0f;
    float mPreviousSpan = 0.0f;
    float mFocusX = 0.0f;
    float mFocusY = 0.0f;
    float mPrevFocusX = 0.0f;
    float mPrevFocusY = 0.0f;

    float mTopStartX = 0.0f;
    float mTopStartY = 0.0f;
    float mBottomStartX = 0.0f;
    float mBottomStartY = 0.0f;
    float mTopFingerDeltaX = 0.0f;
    float mTopFingerDeltaY = 0.0f;
    float mBottomFingerDeltaX = 0.0f;
    float mBottomFingerDeltaY = 0.0f;
};
