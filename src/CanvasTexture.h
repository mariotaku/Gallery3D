// Port of com.cooliris.media.CanvasTexture. Widgets compose a premultiplied Bitmap
// through Canvas; Texture and RenderView manage uploads, sizing and binding.
#pragma once

#include "Texture.h"

class CanvasTexture : public Texture {
  public:
    // The size of the canvas in pixels. RenderView pads the upload to a power
    // of two and works out the extents.
    void setSize(int width, int height);

    int getCanvasWidth() const {
        return mCanvasWidth;
    }

    int getCanvasHeight() const {
        return mCanvasHeight;
    }

    // Throws away the current contents so the next bind redraws them. Cheap to
    // call when nothing changed, since it only does work once.
    void setNeedsDraw();

    // Chrome is small and drawn on demand, so it goes through the cached queue
    // rather than the expensive one.
    bool isCached() const override {
        return true;
    }

    Bitmap load(RenderView *view) override;

  protected:
    // Draw the widget here. The bitmap starts fully transparent and is
    // premultiplied, which is what the Canvas helpers expect.
    virtual void renderCanvas(Bitmap &canvas, int width, int height) = 0;

    virtual void onCanvasSizeChanged() {}

  private:
    int mCanvasWidth = 0;
    int mCanvasHeight = 0;
};
