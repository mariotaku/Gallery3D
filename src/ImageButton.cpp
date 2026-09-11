#include "ImageButton.h"

#include <SDL3/SDL.h>

#include "App.h"

namespace {

// How far past its own bounds a pressed button keeps following the pointer.
const float TRACKING_MARGIN = 30.0f;

}  // namespace

void ImageButton::setImages(const std::string &image, const std::string &pressedImage) {
    mImage = image;
    mPressedImage = pressedImage;
    if (!mPressed) {
        setImage(image, true);
    }
}

void ImageButton::sizeToImage(RenderView *view) {
    if (mImage.empty()) {
        return;
    }
    TexturePtr texture = view->getResource(mImage);
    view->loadTexture(texture);
    if (texture->isLoaded()) {
        setSize((float)texture->getWidth(), (float)texture->getHeight());
    }
}

bool ImageButton::containsPoint(float x, float y, bool addTrackingMargin) const {
    if (mImage.empty()) {
        return false;
    }
    float minX = mX;
    float minY = mY;
    float maxX = minX + mWidth;
    float maxY = minY + mHeight;
    if (addTrackingMargin) {
        float margin = TRACKING_MARGIN * App::UI_DENSITY;
        minX -= margin;
        minY -= margin;
        maxX += margin;
        maxY += margin;
    }
    return x >= minX && y >= minY && x < maxX && y < maxY;
}

bool ImageButton::containsPoint(float x, float y) {
    return containsPoint(x, y, false);
}

void ImageButton::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.updateList.push_back(this);
    lists.blendedList.push_back(this);
    lists.hitTestList.push_back(this);
}

void ImageButton::renderBlended(RenderView *view) {
    if (mCurrentImage.empty()) {
        return;
    }
    float ratio = mFade.getValue(view->getFrameTime());
    TexturePtr current = view->getResource(mCurrentImage);
    view->loadTexture(current);
    if (!current->isLoaded()) {
        return;
    }
    // Draw art at native size; the layer rectangle controls hit testing.
    // The top-right tab's hit rectangle is half the art's height.
    float width = (float)current->getWidth();
    float height = (float)current->getHeight();

    TexturePtr previous = mPreviousImage.empty() ? nullptr : view->getResource(mPreviousImage);
    if (previous != nullptr) {
        view->loadTexture(previous);
    }
    if (ratio >= 0.99f || previous == nullptr || !previous->isLoaded()) {
        if (view->bind(current)) {
            view->draw2D(mX, mY, 0.0f, width, height);
        }
        return;
    }
    if (view->bindMixed(previous, current, ratio)) {
        view->draw2D(mX, mY, 0.0f, width, height);
    }
    view->unbindMixed();
}

bool ImageButton::onTouchEvent(const MotionEvent &event) {
    switch (event.getAction()) {
    case MotionEvent::ACTION_DOWN:
    case MotionEvent::ACTION_MOVE: {
        // With the margin, so a pointer that drifts off the art by a little
        // keeps the button pressed.
        bool hit = containsPoint(event.getX(), event.getY(), true);
        mPressed = hit;
        if (hit) {
            // No crossfade going down: the press has to look immediate.
            setImage(mPressedImage, false);
        } else {
            setImage(mImage, true);
        }
        break;
    }
    case MotionEvent::ACTION_UP:
        if (mPressed && mAction) {
            // Copied first, in case the action tears down the button.
            Action action = mAction;
            mPressed = false;
            setImage(mImage, true);
            action();
            return true;
        }
        mPressed = false;
        setImage(mImage, true);
        break;
    case MotionEvent::ACTION_CANCEL:
        mPressed = false;
        setImage(mImage, true);
        break;
    default:
        break;
    }
    return true;
}

void ImageButton::setImage(const std::string &image, bool animate) {
    if (mCurrentImage == image) {
        return;
    }
    if (animate) {
        mFade.setValue(0.0f);
        mFade.animateValue(1.0f, 0.25f, SDL_GetTicks());
        mPreviousImage = mCurrentImage;
    } else {
        mFade.setValue(1.0f);
        mPreviousImage.clear();
    }
    mCurrentImage = image;
}
