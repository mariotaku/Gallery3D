// Port of com.cooliris.media.CanvasTexture: a texture whose contents a widget
// draws, rather than one loaded from a file.
//
// Two deliberate differences from the original.
//
// It subclasses Texture here. The original could not, because its Texture was
// tied to the asynchronous load queue; it therefore carried its own GL id,
// power of two sizing, normalized extents and bind path, which is most of its
// 198 lines. RenderView already does all of that for a Texture, and
// RenderView::loadTexture runs the load synchronously, which is what chrome
// wants. So the subclass gets it for free and the class shrinks to the part
// that matters: renderCanvas.
//
// And it draws through Canvas rather than android.graphics.Canvas. Subclasses
// get a premultiplied Bitmap and compose into it.
//
// In the original the only subclass was PopupMenu. Here it is meant as the
// shared base for HUD chrome, because compositing a widget once into a bitmap
// is simpler than the original's many small draw2D calls per widget: the nine
// patch art the original stretched at draw time gets stretched once here, into
// the bitmap.
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
