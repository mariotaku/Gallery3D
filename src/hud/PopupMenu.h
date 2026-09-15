// Port of com.cooliris.media.PopupMenu: icon/title rows with a pointer triangle.
// The input rectangle covers the window to dismiss outside presses;
// the drawing rectangle covers only the popup.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "graphics/Canvas.h"
#include "graphics/Bitmap.h"
#include "graphics/CanvasTexture.h"
#include "core/FloatAnim.h"
#include "graphics/Layer.h"
#include "graphics/RenderView.h"

class PopupMenu : public Layer {
  public:
    using Action = std::function<void()>;

    struct Option {
        std::string title;
        std::string icon;
        Action action;
        // Drawn in place of the named drawable when valid, such as another
        // app's icon.
        Bitmap iconBitmap;
    };

    // The side of the square a row's icon is drawn in, in pixels.
    static int iconPixels();

    PopupMenu();
    ~PopupMenu() override;

    void setOptions(const std::vector<Option> &options);

    // Opens at (pointX, pointY), constrained to the safe-area band
    // [boundsLeft, boundsLeft + boundsWidth].
    void showAtPoint(float pointX, float pointY, float boundsLeft, float boundsWidth);
    // The same, opening downward from a point above it, such as a crumb in the
    // top bar.
    void showBelowPoint(float pointX, float pointY, float boundsLeft, float boundsWidth);
    void close(bool fadeOut);

    bool isShowing() const {
        return mShow;
    }

    void generate(RenderView *view, RenderLists &lists) override;
    bool update(RenderView *view, float frameInterval) override;
    void renderBlended(RenderView *view) override;
    // Lays out and composes the widget into a bitmap without GL.
    Bitmap compose();
    bool onTouchEvent(const MotionEvent &event) override;

    // The middle of one row, for a press that has no pointer behind it. -1 for
    // a row that is not there.
    bool rowCenter(size_t index, float *x, float *y) const;

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
    void show(float pointX, float pointY, float boundsLeft, float boundsWidth, bool below);
    void layout();
    int hitTestOptions(float x, float y) const;
    void setSelectedItem(int index);

    std::vector<Row> mRows;
    std::shared_ptr<PopupTexture> mTexture;
    Canvas::NinePatch mHighlight;
    bool mArtLoaded = false;

    // Where the popup itself is drawn, as opposed to the layer rect, which
    // covers the window so that a press outside can close it.
    float mPopupX = 0.0f;
    float mPopupY = 0.0f;
    float mPopupWidth = 0.0f;
    float mPopupHeight = 0.0f;
    float mTriangleX = 0.0f;
    // Whether it opens downward, with its point on top.
    bool mBelow = false;

    bool mNeedsLayout = false;
    bool mShow = false;
    FloatAnim mShowAnim{0.0f};
    int mSelectedItem = -1;
};
