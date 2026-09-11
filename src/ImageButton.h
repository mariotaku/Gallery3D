// Port of com.cooliris.media.ImageButton.
// Crossfades normal/pressed art and tracks presses beyond its bounds by a margin.
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
