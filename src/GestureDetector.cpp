#include "GestureDetector.h"

#include <cmath>

bool GestureDetector::onTouchEvent(const MotionEvent &event) {
    switch (event.action) {
    case MotionEvent::ACTION_DOWN: {
        mDown = true;
        mMoved = false;
        mLongPressFired = false;
        mDownEvent = event;
        mLastEvent = event;
        mDownTime = event.eventTime;
        mLastMoveTime = event.eventTime;
        mVelocityX = 0.0f;
        mVelocityY = 0.0f;
        mListener->onDown(event);
        return true;
    }
    case MotionEvent::ACTION_MOVE: {
        if (!mDown) {
            return false;
        }
        float dx = event.getX() - mLastEvent.getX();
        float dy = event.getY() - mLastEvent.getY();
        uint64_t dt = (event.eventTime > mLastMoveTime) ? (event.eventTime - mLastMoveTime) : 1;
        // Pixels per second, smoothed a little so a single jittery sample does
        // not dominate the fling.
        float instantX = dx * 1000.0f / (float)dt;
        float instantY = dy * 1000.0f / (float)dt;
        mVelocityX = mVelocityX * 0.6f + instantX * 0.4f;
        mVelocityY = mVelocityY * 0.6f + instantY * 0.4f;
        mLastMoveTime = event.eventTime;

        if (std::fabs(event.getX() - mDownEvent.getX()) > TOUCH_SLOP ||
            std::fabs(event.getY() - mDownEvent.getY()) > TOUCH_SLOP) {
            mMoved = true;
        }
        mListener->onScroll(mDownEvent, event, -dx, -dy);
        mLastEvent = event;
        return true;
    }
    case MotionEvent::ACTION_UP: {
        if (!mDown) {
            return false;
        }
        mDown = false;
        if (mLongPressFired) {
            return true;
        }
        if (mMoved) {
            const float kMinFlingVelocity = 120.0f;
            if (std::fabs(mVelocityX) > kMinFlingVelocity || std::fabs(mVelocityY) > kMinFlingVelocity) {
                mListener->onFling(mDownEvent, event, mVelocityX, mVelocityY);
            }
            return true;
        }
        bool isDoubleTap = (event.eventTime - mPreviousUpTime) < DOUBLE_TAP_TIMEOUT_MS &&
                           std::fabs(event.getX() - mPreviousUpX) < TOUCH_SLOP * 2 &&
                           std::fabs(event.getY() - mPreviousUpY) < TOUCH_SLOP * 2;
        if (isDoubleTap) {
            mPreviousUpTime = 0;
            mListener->onDoubleTap(event);
        } else {
            mPreviousUpTime = event.eventTime;
            mPreviousUpX = event.getX();
            mPreviousUpY = event.getY();
            mListener->onSingleTapUp(event);
        }
        return true;
    }
    case MotionEvent::ACTION_CANCEL:
        mDown = false;
        return true;
    default:
        return false;
    }
}

void GestureDetector::update(uint64_t nowMs) {
    if (mDown && mLongpressEnabled && !mMoved && !mLongPressFired &&
        (nowMs - mDownTime) >= LONGPRESS_TIMEOUT_MS) {
        mLongPressFired = true;
        mListener->onLongPress(mDownEvent);
    }
}

// ---------------------------------------------------------------------------
// ScaleGestureDetector
// ---------------------------------------------------------------------------

void ScaleGestureDetector::reset() {
    mInProgress = false;
    mScaleFactor = 1.0f;
    mCurrentSpan = 0.0f;
    mPreviousSpan = 0.0f;
    mTopFingerDeltaX = 0.0f;
    mTopFingerDeltaY = 0.0f;
    mBottomFingerDeltaX = 0.0f;
    mBottomFingerDeltaY = 0.0f;
}

bool ScaleGestureDetector::onTouchEvent(const MotionEvent &event) {
    if (event.pointerCount < 2) {
        if (mInProgress) {
            mListener->onScaleEnd(this, false);
            reset();
        }
        return false;
    }

    float x0 = event.getX(0);
    float y0 = event.getY(0);
    float x1 = event.getX(1);
    float y1 = event.getY(1);
    float dx = x1 - x0;
    float dy = y1 - y0;
    float span = std::sqrt(dx * dx + dy * dy);
    mPrevFocusX = mInProgress ? mFocusX : (x0 + x1) * 0.5f;
    mPrevFocusY = mInProgress ? mFocusY : (y0 + y1) * 0.5f;
    mFocusX = (x0 + x1) * 0.5f;
    mFocusY = (y0 + y1) * 0.5f;

    // The top finger is the one nearer the top of the screen, matching the
    // original naming.
    bool firstIsTop = y0 <= y1;
    float topX = firstIsTop ? x0 : x1;
    float topY = firstIsTop ? y0 : y1;
    float bottomX = firstIsTop ? x1 : x0;
    float bottomY = firstIsTop ? y1 : y0;

    if (!mInProgress) {
        mInProgress = true;
        mPreviousSpan = span;
        mCurrentSpan = span;
        mScaleFactor = 1.0f;
        mTopStartX = topX;
        mTopStartY = topY;
        mBottomStartX = bottomX;
        mBottomStartY = bottomY;
        mTopFingerDeltaX = 0.0f;
        mTopFingerDeltaY = 0.0f;
        mBottomFingerDeltaX = 0.0f;
        mBottomFingerDeltaY = 0.0f;
        mListener->onScaleBegin(this);
        return true;
    }

    mCurrentSpan = span;
    mScaleFactor = (mPreviousSpan > 0.0f) ? (span / mPreviousSpan) : 1.0f;
    mPreviousSpan = span;
    mTopFingerDeltaX = topX - mTopStartX;
    mTopFingerDeltaY = topY - mTopStartY;
    mBottomFingerDeltaX = bottomX - mBottomStartX;
    mBottomFingerDeltaY = bottomY - mBottomStartY;

    if (event.action == MotionEvent::ACTION_UP || event.action == MotionEvent::ACTION_POINTER_UP) {
        mListener->onScaleEnd(this, false);
        reset();
        return true;
    }
    mListener->onScale(this);
    return true;
}

void ScaleGestureDetector::onWheel(float focusX, float focusY, float ticks) {
    if (ticks == 0.0f) {
        return;
    }
    mPrevFocusX = focusX;
    mPrevFocusY = focusY;
    mFocusX = focusX;
    mFocusY = focusY;
    if (!mInProgress) {
        mInProgress = true;
        mCurrentSpan = 200.0f;
        mPreviousSpan = mCurrentSpan;
        mScaleFactor = 1.0f;
        mTopFingerDeltaX = 0.0f;
        mTopFingerDeltaY = 0.0f;
        mBottomFingerDeltaX = 0.0f;
        mBottomFingerDeltaY = 0.0f;
        mListener->onScaleBegin(this);
    }
    // One wheel tick is worth a 12 percent pinch.
    mScaleFactor = std::pow(1.12f, ticks);
    mPreviousSpan = mCurrentSpan;
    mCurrentSpan *= mScaleFactor;
    mListener->onScale(this);
    mListener->onScaleEnd(this, false);
    reset();
}
