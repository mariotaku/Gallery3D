#include "HudLayer.h"

#include "App.h"

void HudLayer::generate(RenderView *view, RenderLists &lists) {
    mPathBar.generate(view, lists);
}

void HudLayer::onSizeChanged() {
    // The bar runs along the top edge. It sizes itself to its crumbs, so what
    // it needs here is the room it may use and where it starts.
    float inset = 3.0f * App::PIXEL_DENSITY;
    mPathBar.setPosition(inset, inset);
    mPathBar.setSize(mWidth - inset * 2.0f, PathBarLayer::preferredHeight());
}
