// Port of com.cooliris.media.LoadingLayer: the dark sheet that covers the wall
// at startup and fades once there is something worth showing.
//
// The original waited on a fixed list of preloaded drawables. Here it waits on
// the same frame art plus the feed's first scan, which is the condition that
// actually matters: without it the sheet lifts on an empty wall and the photos
// pop in behind it.
//
// It draws a flat colour, and the renderer has no untextured path - everything
// goes through the texture shaders. So it fills a small CanvasTexture with
// white and tints it, which is also why CanvasTexture earns its keep here.
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
