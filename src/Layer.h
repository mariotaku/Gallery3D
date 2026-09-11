// Port of com.cooliris.media.Layer and RootLayer.
#pragma once

#include "Input.h"

class RenderView;

class Layer {
  public:
    virtual ~Layer() = default;

    float getX() const {
        return mX;
    }

    float getY() const {
        return mY;
    }

    void setPosition(float x, float y) {
        mX = x;
        mY = y;
    }

    float getWidth() const {
        return mWidth;
    }

    float getHeight() const {
        return mHeight;
    }

    void setSize(float width, float height) {
        if (mWidth != width || mHeight != height) {
            mWidth = width;
            mHeight = height;
            onSizeChanged();
        }
    }

    // Runs the layout again even though the box has not changed. A density
    // change moves everything inside a window that stayed exactly the same
    // size, and setSize above would take that for nothing to do.
    void relayout() {
        onSizeChanged();
    }

    bool isHidden() const {
        return mHidden;
    }

    void setHidden(bool hidden) {
        if (mHidden != hidden) {
            mHidden = hidden;
            onHiddenChanged();
        }
    }

    // Adds this layer to the render view's draw lists.
    virtual void generate(RenderView *view, class RenderLists &lists) = 0;

    // Returns true if something is animating.
    virtual bool update(RenderView *view, float frameInterval) {
        (void)view;
        (void)frameInterval;
        return false;
    }

    virtual void renderOpaque(RenderView *view) {
        (void)view;
    }

    virtual void renderBlended(RenderView *view) {
        (void)view;
    }

    virtual bool onTouchEvent(const MotionEvent &event) {
        (void)event;
        return false;
    }

    // Narrows the hit test past the layer bounds.
    virtual bool containsPoint(float x, float y) {
        (void)x;
        (void)y;
        return true;
    }

    virtual void onSurfaceCreated(RenderView *view) {
        (void)view;
    }

    float mX = 0.0f;
    float mY = 0.0f;
    float mWidth = 0.0f;
    float mHeight = 0.0f;
    bool mHidden = false;

  protected:
    virtual void onSizeChanged() {}
    virtual void onHiddenChanged() {}
};

class RootLayer : public Layer {
  public:
    virtual bool onKeyDown(int keyCode, const KeyEvent &event) {
        (void)keyCode;
        (void)event;
        return false;
    }

    virtual void onSurfaceChanged(RenderView *view, int width, int height) {
        (void)view;
        (void)width;
        (void)height;
    }

    // Where the pointer is, with no button held. Separate from the touch queue
    // on purpose: that one models a finger, and a finger is either down or not
    // there at all. Only chrome that lights under the pointer wants this, so
    // the default is to ignore it.
    virtual void onPointerMoved(float x, float y) {
        (void)x;
        (void)y;
    }

    // The accelerometer, in metres per second squared, already rotated into the
    // display's orientation. Only a device with one ever calls this.
    virtual void onAccelerometer(float x, float y, float z) {
        (void)x;
        (void)y;
        (void)z;
    }

    virtual void handleLowMemory() {}
};
