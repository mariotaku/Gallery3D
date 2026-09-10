// Port of com.cooliris.media.PathBarLayer: the breadcrumb bar along the top.
//
// The original drew the bar with a handful of draw2D calls per segment - fill,
// join, cap, icon, label. Here the whole bar is composed once into a
// CanvasTexture and blitted in one call, and it is only recomposed when the
// crumbs or the size change. That is the same picture with less per frame work,
// and it gives the port's CanvasTexture a real consumer.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CanvasTexture.h"
#include "Layer.h"
#include "RenderView.h"

class PathBarLayer : public Layer {
  public:
    // Run when the crumb is clicked. The original passed a Runnable.
    using Action = std::function<void()>;

    PathBarLayer();
    ~PathBarLayer() override;

    void pushLabel(const char *icon, const std::string &label, Action action = nullptr);
    void popLabel();
    void changeLabel(const std::string &label);
    std::string getCurrentLabel() const;
    int getNumLevels() const;
    void clear();

    // The original cycled a spinner here while the media scanner ran. Nothing
    // drives it yet, so it stays a no-op rather than a lie.
    void setAnimatedIcons(const void *icons) {
        (void)icons;
    }

    // The height the bar wants, in pixels. The art is 39 tall at density 1.
    static float preferredHeight();

    void generate(RenderView *view, RenderLists &lists) override;
    void renderBlended(RenderView *view) override;
    bool onTouchEvent(const MotionEvent &event) override;
    bool containsPoint(float x, float y) override;

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
