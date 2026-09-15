#include "hud/LoadingLayer.h"

#include <algorithm>

#include "graphics/Canvas.h"
#include "grid/GridLayer.h"
#include "media/MediaFeed.h"

namespace {


const float FADE_INTERVAL = 0.5f;
const float GRAY_VALUE = 0.1f;

}  // namespace

LoadingLayer::LoadingLayer(GridLayer *gridLayer) : mGridLayer(gridLayer) {
    mSheet = std::make_shared<SheetTexture>();
    mSheet->setSize(2, 2);
}

void LoadingLayer::SheetTexture::renderCanvas(Bitmap &canvas, int width, int height) {
    Canvas::fillRect(canvas, 0, 0, width, height, 1.0f, 1.0f, 1.0f, 1.0f);
}

void LoadingLayer::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.updateList.push_back(this);
    lists.blendedList.push_back(this);
}

void LoadingLayer::reset() {
    mLoaded = false;
    mOpacity = 1.0f;
    setHidden(false);
}

bool LoadingLayer::isContentReady(RenderView *view) const {
    // The frame art is what every grid item is drawn with, so an early lift
    // would show unframed thumbnails.
    static const char *const kRequired[] = {
        Res::drawable::stack_frame,
        Res::drawable::grid_frame,
        Res::drawable::stack_frame_focus,
        Res::drawable::stack_frame_gold,
    };
    for (const char *name : kRequired) {
        TexturePtr texture = view->getResource(name, false);
        if (!texture || !texture->isLoaded()) {
            return false;
        }
    }
    // And wait for the first album, or the end of the first scan when there is
    // none, or the sheet lifts on an empty wall. A source that lists album by
    // album, such as a large folder tree, shows each one as it arrives rather
    // than behind the sheet until the last.
    MediaFeed *feed = mGridLayer->getFeed();
    return feed != nullptr && (!feed->isLoading() || !feed->getMediaSets().empty());
}

bool LoadingLayer::update(RenderView *view, float frameInterval) {
    if (mHidden) {
        return false;
    }
    if (!mLoaded) {
        if (isContentReady(view)) {
            mLoaded = true;
        }
        // Hold at full opacity until then, and keep asking for frames.
        return true;
    }
    if (mOpacity > 0.0f) {
        mOpacity -= frameInterval / FADE_INTERVAL;
        if (mOpacity <= 0.0f) {
            mOpacity = 0.0f;
            setHidden(true);
        }
        return true;
    }
    return false;
}

void LoadingLayer::renderBlended(RenderView *view) {
    if (mHidden || mOpacity <= 0.004f) {
        return;
    }
    if (!mSheet->isLoaded()) {
        view->loadTexture(mSheet);
        if (!mSheet->isLoaded()) {
            return;
        }
    }
    // Premultiplied colour; disable depth writes so the final overlay leaves no near-plane
    // depth.
    float gray = GRAY_VALUE * mOpacity;
    glDepthMask(GL_FALSE);
    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    view->setColor(gray, gray, gray, mOpacity);
    view->draw2D(mSheet, 0.0f, 0.0f, (float)view->getWidth(), (float)view->getHeight());
    view->resetColor();
    glDepthMask(GL_TRUE);
}
