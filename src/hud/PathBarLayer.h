// Port of com.cooliris.media.PathBarLayer: top breadcrumb bar.
// Caches the composition in CanvasTexture until crumbs or dimensions change.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "graphics/CanvasTexture.h"
#include "graphics/Layer.h"
#include "graphics/RenderView.h"

class PathBarLayer : public Layer {
  public:
    // Action run when the crumb is clicked.
    using Action = std::function<void()>;

    PathBarLayer();
    ~PathBarLayer() override;

    void pushLabel(const char *icon, const std::string &label, Action action = nullptr);
    void popLabel();
    void changeLabel(const std::string &label);
    // Renames the crumb at index, such as the home crumb, which is not always
    // the current one.
    void changeLabelAt(size_t index, const std::string &label);
    // The middle of the crumb at index, in window pixels, once the bar has
    // been laid out. The bar's left edge before that.
    float crumbCenterX(size_t index) const;
    std::string getCurrentLabel() const;
    int getNumLevels() const;
    void clear();

    // Scanner spinner hook; currently a no-op.
    void setAnimatedIcons(const void *icons) {
        (void)icons;
    }

    // The height the bar wants, in pixels. The art is 39 tall at density 1.
    static float preferredHeight();

    void generate(RenderView *view, RenderLists &lists) override;
    void renderBlended(RenderView *view) override;
    // Lays out and composes the widget into a bitmap without GL.
    Bitmap compose();
    bool onTouchEvent(const MotionEvent &event) override;
    bool containsPoint(float x, float y) override;

    // Actual crumb extent, used as the start of the window's draggable area.
    float barRightEdge() const {
        return mX + mBarWidth;
    }

  protected:
    void onSizeChanged() override;

  private:
    struct Component {
        std::string label;
        std::string icon;
        Action action;
        // Filled in by the layout pass, in bar-local pixels.
        float x = 0.0f;
        float width = 0.0f;
    };

    // Composes the whole bar. Holds a back pointer because the layout it draws
    // lives in the layer.
    class BarTexture : public CanvasTexture {
      public:
        explicit BarTexture(PathBarLayer *owner) : mOwner(owner) {}

      protected:
        void renderCanvas(Bitmap &canvas, int width, int height) override;

      private:
        PathBarLayer *mOwner;
    };

    // Works out each crumb's width and the total, and resizes the texture.
    void layout();
    void invalidate();

    std::vector<Component> mComponents;
    std::shared_ptr<BarTexture> mTexture;
    int mTouchIndex = -1;
    float mBarWidth = 0.0f;
    bool mNeedsLayout = true;
};
