// Port of com.cooliris.media.BackgroundLayer.
//
// The original crossfaded a blurred, colour matched backdrop generated from the
// photo under the cursor. This keeps the stitching, the parallax and the far
// plane placement, and uses the shipped default background instead of the
// adaptive one; the adaptive version belongs with the rest of the HUD work.
#pragma once

#include "Layer.h"
#include "RenderView.h"

class GridLayer;

class BackgroundLayer : public Layer {
  public:
    explicit BackgroundLayer(GridLayer *layer) : mGridLayer(layer) {}

    void generate(RenderView *view, RenderLists &lists) override;
    void renderOpaque(RenderView *view) override;
    void renderBlended(RenderView *view) override;

    void clear() {
        mBackground.reset();
    }

    void clearCache() {}

  protected:
    void onSizeChanged() override;

  private:
    GridLayer *mGridLayer;
    TexturePtr mBackground;
    int mBackgroundBlitWidth = 0;
    int mBackgroundOverlap = 0;
};
