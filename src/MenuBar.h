// Port of com.cooliris.media.MenuBar: selection actions along the bottom.
// Each button centres its icon/label and shows highlight art while pressed.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Bitmap.h"
#include "CanvasTexture.h"
#include "Layer.h"
#include "RenderView.h"

class GridLayer;

class MenuBar : public Layer {
  public:
    using Action = std::function<void()>;

    MenuBar();
    ~MenuBar() override;

    struct ButtonSpec {
        // Either may be empty. A button with neither draws nothing but still
        // takes its share of the bar, which is how the original spaced things.
        std::string icon;
        std::string label;
        Action action;
    };

    // Rebuilt whenever the mode changes, so the bar carries the right actions.
    void setButtons(const std::vector<ButtonSpec> &buttons);
    void clearButtons();

    // Change a label without rebuilding, preserving the current press.
    void setButtonLabel(size_t index, const std::string &label);

    static float preferredHeight();

    // Where a button sits, in window coordinates. A button that opens a popup
    // needs this to point the popup back at itself.
    float buttonCenterX(size_t index) const;

    void generate(RenderView *view, RenderLists &lists) override;
    void renderBlended(RenderView *view) override;
    // Lays out and composes the widget into a bitmap without GL.
    Bitmap compose();
    bool onTouchEvent(const MotionEvent &event) override;
    bool containsPoint(float x, float y) override;

  protected:
    void onSizeChanged() override;

  private:
    struct Button {
        std::string icon;
        std::string label;
        Action action;
        float x = 0.0f;
        float width = 0.0f;
    };

    class BarTexture : public CanvasTexture {
      public:
        explicit BarTexture(MenuBar *owner) : mOwner(owner) {}

      protected:
        void renderCanvas(Bitmap &canvas, int width, int height) override;

      private:
        void drawHighlight(Bitmap &canvas, const Button &button, int height);

        MenuBar *mOwner;
    };

    void layout();

    std::vector<Button> mButtons;
    // Which button is held down, so the bar can light it. -1 for none.
    int mPressedIndex = -1;
    std::shared_ptr<BarTexture> mTexture;
    bool mNeedsLayout = true;
};
