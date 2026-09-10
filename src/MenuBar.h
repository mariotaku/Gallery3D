// Partial port of com.cooliris.media.MenuBar: the bar along the bottom that
// carries the actions for whatever is selected.
//
// This is the bar and its buttons. The original also had SelectionMenu and
// PopupMenu hanging off it, a dropdown per button; that is still to do, so each
// button here runs its action directly instead of opening a menu.
//
// A button carries an icon, a label or both. Whichever it has sits centred in
// the button, and a press lights it with the highlight art.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

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

    static float preferredHeight();

    void generate(RenderView *view, RenderLists &lists) override;
    void renderBlended(RenderView *view) override;
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
