// Port of com.cooliris.media.PopupMenu: the dropdown a bar button opens, a
// column of icon and title rows over a nine-patch with a triangle pointing back
// at the button that opened it.
//
// This is what CanvasTexture was written for in the original, and it is the
// last widget to use it here.
//
// One deliberate difference. The original put the popup in the render view's
// system list, which saw input before anything else, so a press outside it
// could close it. There is no system list in the input path here, so while the
// popup is open its layer rect is the whole window and it decides for itself
// whether a press landed on a row or outside. What it draws is its own rect,
// which is why the two are tracked separately.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Canvas.h"
#include "Bitmap.h"
#include "CanvasTexture.h"
#include "FloatAnim.h"
#include "Layer.h"
#include "RenderView.h"

class PopupMenu : public Layer {
  public:
    using Action = std::function<void()>;

    struct Option {
        std::string title;
        std::string icon;
        Action action;
    };

    PopupMenu();
    ~PopupMenu() override;

    void setOptions(const std::vector<Option> &options);

    // Opens the popup pointing at (pointX, pointY), kept inside the band from
    // boundsLeft to boundsLeft + boundsWidth. That band is the safe rect, not
    // the window: a popup pushed against a notch is one you cannot read.
    void showAtPoint(float pointX, float pointY, float boundsLeft, float boundsWidth);
    void close(bool fadeOut);

    bool isShowing() const {
        return mShow;
    }

    void generate(RenderView *view, RenderLists &lists) override;
    bool update(RenderView *view, float frameInterval) override;
    void renderBlended(RenderView *view) override;
    // Lays the widget out and composes it into a bitmap, without touching GL.
    // renderBlended does exactly this before handing the result to the
    // renderer, so a test can look at the same pixels the screen gets.
    Bitmap compose();
    bool onTouchEvent(const MotionEvent &event) override;

  private:
    struct Row {
        Option option;
        // In popup coordinates.
        float top = 0.0f;
        float bottom = 0.0f;
    };

    class PopupTexture : public CanvasTexture {
      public:
        explicit PopupTexture(PopupMenu *owner) : mOwner(owner) {}

      protected:
        void renderCanvas(Bitmap &canvas, int width, int height) override;

      private:
        PopupMenu *mOwner;
    };

    void ensureArt();
    void layout();
    int hitTestOptions(float x, float y) const;
    void setSelectedItem(int index);

    std::vector<Row> mRows;
    std::shared_ptr<PopupTexture> mTexture;
    Canvas::NinePatch mBackground;
    Canvas::NinePatch mHighlight;
    Bitmap mTriangle;
    bool mArtLoaded = false;

    // Where the popup itself is drawn, as opposed to the layer rect, which
    // covers the window so that a press outside can close it.
    float mPopupX = 0.0f;
    float mPopupY = 0.0f;
    float mPopupWidth = 0.0f;
    float mPopupHeight = 0.0f;
    float mTriangleX = 0.0f;

    bool mNeedsLayout = false;
    bool mShow = false;
    FloatAnim mShowAnim{0.0f};
    int mSelectedItem = -1;
};
