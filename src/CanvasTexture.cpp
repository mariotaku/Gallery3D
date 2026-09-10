#include "CanvasTexture.h"

#include "RenderView.h"

void CanvasTexture::setSize(int width, int height) {
    if (width == mCanvasWidth && height == mCanvasHeight) {
        return;
    }
    mCanvasWidth = width;
    mCanvasHeight = height;
    onCanvasSizeChanged();
    setNeedsDraw();
}

void CanvasTexture::setNeedsDraw() {
    if (mState == STATE_UNLOADED) {
        return;
    }
    // Hand the old GL texture back before forgetting its id, or redrawing a
    // widget every time its label changes would leak one texture per change.
    if (mId != 0 && mOwner != nullptr) {
        mOwner->queueDeleteTexture(mId);
    }
    mId = 0;
    mState = STATE_UNLOADED;
}

Bitmap CanvasTexture::load(RenderView *view) {
    (void)view;
    if (mCanvasWidth <= 0 || mCanvasHeight <= 0) {
        return Bitmap();
    }
    Bitmap canvas(mCanvasWidth, mCanvasHeight);
    renderCanvas(canvas, mCanvasWidth, mCanvasHeight);
    return canvas;
}
