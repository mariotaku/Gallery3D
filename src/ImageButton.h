// Port of com.cooliris.media.ImageButton: a HUD button with a normal and a
// pressed image, and an action it runs when a press ends on it.
//
// The press does not swap images outright, it crossfades. And once a press has
// started the button keeps tracking the pointer past its own bounds by a
// margin, so a finger that slides a little does not lose the button.
#pragma once

#include <functional>
#include <string>

#include "FloatAnim.h"
#include "Layer.h"
#include "RenderView.h"

class ImageButton : public Layer {
  public:
    using Action = std::function<void()>;

    void setImages(const std::string &image, const std::string &pressedImage);
    void setAction(Action action) {
        mAction = std::move(action);
    }

    // Sizes the button to its own art, which is what the callers want when they
    // have not picked a size themselves.
    void sizeToImage(RenderView *view);

    void generate(RenderView *view, RenderLists &lists) override;
    void renderBlended(RenderView *view) override;
    bool onTouchEvent(const MotionEvent &event) override;
    bool containsPoint(float x, float y) override;

  private:
    bool containsPoint(float x, float y, bool addTrackingMargin) const;
    void setImage(const std::string &image, bool animate);

    std::string mImage;
    std::string mPressedImage;
    Action mAction;

    FloatAnim mFade{1.0f};
    std::string mCurrentImage;
    std::string mPreviousImage;
    bool mPressed = false;
};
