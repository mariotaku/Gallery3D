#include "HudLayer.h"

#include <SDL3/SDL.h>

#include "App.h"
#include "GridLayer.h"
#include "FloatUtils.h"

namespace {

// Idle time in fullscreen before the chrome gets out of the way.
const uint64_t AUTO_HIDE_MS = 5000;

}  // namespace

void HudLayer::generate(RenderView *view, RenderLists &lists) {
    lists.updateList.push_back(this);
    // Before the bars, not after: renderBlended sets the alpha they are then
    // drawn with, so the whole HUD fades as one.
    lists.blendedList.push_back(this);
    mPathBar.generate(view, lists);
    mMenuBar.generate(view, lists);
}

void HudLayer::onSizeChanged() {
    // The bar runs along the top edge. It sizes itself to its crumbs, so what
    // it needs here is the room it may use and where it starts.
    float inset = 3.0f * App::PIXEL_DENSITY;
    mPathBar.setPosition(inset, inset);
    mPathBar.setSize(mWidth - inset * 2.0f, PathBarLayer::preferredHeight());

    // The menu bar runs along the bottom edge.
    mMenuBar.setPosition(0.0f, mHeight - MenuBar::preferredHeight());
    mMenuBar.setSize(mWidth, MenuBar::preferredHeight());
}

void HudLayer::computeBottomMenu() {
    if (mGridLayer == nullptr || mMode != MODE_SELECT) {
        mMenuBar.clearButtons();
        return;
    }
    GridLayer *grid = mGridLayer;
    std::vector<std::pair<std::string, MenuBar::Action>> buttons;
    buttons.emplace_back("ic_menu_rotate_left", [grid]() { grid->rotateSelectedItems(-90.0f); });
    buttons.emplace_back("ic_menu_rotate_right", [grid]() { grid->rotateSelectedItems(90.0f); });
    if (!grid->noDeleteMode()) {
        buttons.emplace_back("icon_delete", [grid]() { grid->deleteSelection(); });
    }
    mMenuBar.setButtons(buttons);
}

void HudLayer::setAlpha(float alpha) {
    mAlpha = alpha;
    if (alpha == 1.0f) {
        // Restart the idle clock. The grid calls this every frame outside
        // fullscreen, which is what keeps the chrome up there.
        mLastTimeFullOpacity = SDL_GetTicks();
    }
}

void HudLayer::setMode(int mode) {
    if (mMode == mode) {
        return;
    }
    mMode = mode;
    // The bar carries the actions for the mode, so it has to follow it.
    computeBottomMenu();
}

void HudLayer::reset() {
    mMode = MODE_NORMAL;
    mMenuBar.clearButtons();
    setAlpha(1.0f);
    mAnimAlpha = 1.0f;
}

void HudLayer::enterSelectionMode() {
    setAlpha(1.0f);
    setMode(MODE_SELECT);
}

void HudLayer::cancelSelection() {
    setMode(MODE_NORMAL);
}

bool HudLayer::update(RenderView *view, float frameInterval) {
    (void)view;
    // Four times faster on the way in than on the way out, so the chrome
    // answers a tap immediately but leaves gently.
    float factor = (mAlpha == 1.0f) ? 4.0f : 1.0f;
    mAnimAlpha = FloatUtils::animate(mAnimAlpha, mAlpha, frameInterval * factor);

    if (mAutoHide && mAlpha == 1.0f && mMode != MODE_SELECT) {
        if (SDL_GetTicks() - mLastTimeFullOpacity >= AUTO_HIDE_MS) {
            setAlpha(0.0f);
        }
    }

    // Once it has faded out there is nothing to draw and nothing to click, so
    // take the bars out of both the render and the hit test lists.
    bool faded = mAnimAlpha <= 0.01f;
    mPathBar.setHidden(faded);
    mMenuBar.setHidden(faded || mMode != MODE_SELECT);

    return mAnimAlpha != mAlpha;
}

void HudLayer::renderBlended(RenderView *view) {
    // Sets the alpha the bars below inherit. They deliberately do not reset the
    // colour, so this is what fades the whole HUD together.
    view->setAlpha(mAnimAlpha);
}
