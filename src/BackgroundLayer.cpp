#include "BackgroundLayer.h"

#include <SDL3/SDL.h>

#include "App.h"
#include "GridLayer.h"

// The backdrop sits at the far end of the depth buffer so everything the grid
// drew in the opaque pass stays in front of it.
static const float Z_FAR_PLANE = 0.9999f;
static const float PARALLAX = 0.5f;

void BackgroundLayer::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.blendedList.push_back(this);
    lists.updateList.push_back(this);
    lists.opaqueList.push_back(this);
}

void BackgroundLayer::renderOpaque(RenderView *view) {
    glClear(GL_COLOR_BUFFER_BIT);
    if (!mBackground) {
        mBackground = view->getResource(Res::drawable::default_background, false);
        view->loadTexture(mBackground);
    }
}

void BackgroundLayer::renderBlended(RenderView *view) {
    if (!mBackground || !mBackground->isLoaded()) {
        return;
    }
    view->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    view->resetColor();
    if (!view->bind(mBackground)) {
        return;
    }

    // Stitched three times so the wrap is covered whichever way the wall scrolled.
    int cameraPosition = (int)(mGridLayer->getScrollPosition() * PARALLAX);
    int backgroundSpacing = mBackgroundBlitWidth - mBackgroundOverlap;
    if (backgroundSpacing <= 0) {
        backgroundSpacing = 1;
    }
    int anchorEdge = -cameraPosition % backgroundSpacing;
    int rightEdge = anchorEdge + backgroundSpacing;
    int leftEdge = anchorEdge - backgroundSpacing;

    view->draw2D((float)rightEdge, 0.0f, Z_FAR_PLANE, (float)mBackgroundBlitWidth, mHeight);
    view->draw2D((float)anchorEdge, 0.0f, Z_FAR_PLANE, (float)mBackgroundBlitWidth, mHeight);
    view->draw2D((float)leftEdge, 0.0f, Z_FAR_PLANE, (float)mBackgroundBlitWidth, mHeight);

    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    view->resetColor();
}

void BackgroundLayer::onSizeChanged() {
    mBackgroundBlitWidth = (int)(mWidth * 1.5f);
    mBackgroundOverlap = (int)((float)mBackgroundBlitWidth * 0.25f);
}
