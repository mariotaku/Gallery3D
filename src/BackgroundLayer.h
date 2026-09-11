// Port of com.cooliris.media.BackgroundLayer.
// Crossfades blurred photos and tiles them with parallax at the far plane.
// Uses the shipped gradient until the first thumbnail loads.
#pragma once

#include <map>
#include <memory>

#include "CrossFadingTexture.h"
#include "Layer.h"
#include "RenderView.h"

class DisplayItem;
class GridLayer;

class BackgroundLayer : public Layer {
  public:
    explicit BackgroundLayer(GridLayer *layer) : mGridLayer(layer) {}

    void generate(RenderView *view, RenderLists &lists) override;
    bool update(RenderView *view, float frameInterval) override;
    void renderOpaque(RenderView *view) override;
    void renderBlended(RenderView *view) override;

    void clear();
    void clearCache();

  protected:
    void onSizeChanged() override;

  private:
    // Returns the adaptive backdrop for this item, or the fallback while its
    // thumbnail is still loading.
    TexturePtr getAdaptive(RenderView *view, DisplayItem *item);

    GridLayer *mGridLayer;
    std::unique_ptr<CrossFadingTexture> mBackground;
    TexturePtr mFallbackBackground;
    // Keyed by the thumbnail the backdrop was derived from, which the key also
    // keeps alive. Bounded: the whole map goes when it fills up.
    std::map<TexturePtr, TexturePtr> mCacheAdaptiveTexture;
    int mCount = 0;
    int mBackgroundBlitWidth = 0;
    int mBackgroundOverlap = 0;
};
