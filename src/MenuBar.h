// Partial port of com.cooliris.media.MenuBar: the bar along the bottom that
// carries the actions for whatever is selected.
//
// This is the bar and its buttons. The original also had SelectionMenu and
// PopupMenu hanging off it, a dropdown per button; that is still to do, so each
// button here runs its action directly instead of opening a menu.
//
// It matters because delete and rotate already work and had no way to be
// invoked short of a debug flag. This is what puts them under the pointer.
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

    // Rebuilt whenever the mode changes, so the bar carries the right actions.
    void setButtons(const std::vector<std::pair<std::string, Action>> &buttons);
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
        MenuBar *mOwner;
    };

    void layout();

    std::vector<Button> mButtons;
    std::shared_ptr<BarTexture> mTexture;
    int mTouchIndex = -1;
    bool mNeedsLayout = true;
};
