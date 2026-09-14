#include "graphics/CanvasTexture.h"

#include "graphics/RenderView.h"

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
    // Release the previous GL texture before replacing its id.
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
    // In the order decoded art comes in, so the art drawn into it is copied
    // without exchanging channels.
    Bitmap canvas(mCanvasWidth, mCanvasHeight, Bitmap::decodeOrder());
    renderCanvas(canvas, mCanvasWidth, mCanvasHeight);
    return canvas;
}
