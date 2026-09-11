// Port of com.cooliris.media.LoadingLayer. Fades the startup sheet after frame art
// and the first feed scan load. Uses a tinted white texture for the texture-only renderer.
#pragma once

#include <memory>

#include "CanvasTexture.h"
#include "Layer.h"
#include "RenderView.h"

class GridLayer;

class LoadingLayer : public Layer {
  public:
    explicit LoadingLayer(GridLayer *gridLayer);

    void generate(RenderView *view, RenderLists &lists) override;
    bool update(RenderView *view, float frameInterval) override;
    void renderBlended(RenderView *view) override;

    void reset();

  private:
    // A flat white square to tint. Two pixels rather than one so the power of
    // two padding leaves nothing to sample.
    class SheetTexture : public CanvasTexture {
      protected:
        void renderCanvas(Bitmap &canvas, int width, int height) override;
    };

    bool isContentReady(RenderView *view) const;

    GridLayer *mGridLayer;
    std::shared_ptr<SheetTexture> mSheet;
    float mOpacity = 1.0f;
    bool mLoaded = false;
};
