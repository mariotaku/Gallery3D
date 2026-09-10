#include "HudLayer.h"

#include <SDL3/SDL.h>

#include "App.h"
#include "GridLayer.h"
#include "FloatUtils.h"

namespace {

// Idle time in fullscreen before the chrome gets out of the way.
const uint64_t AUTO_HIDE_MS = 5000;

// The top right button is a wide tab. It is half as tall inside an album,
// which is what the original did to keep it clear of the thumbnails.
const float TOP_RIGHT_WIDTH = 100.0f;
const float TOP_RIGHT_HEIGHT = 94.0f;
const float ZOOM_BUTTON_WIDTH = 66.666f;
const float ZOOM_BUTTON_HEIGHT = 42.0f;

std::string selectionCountLabel(int count) {
    return std::to_string(count) + ((count == 1) ? " item" : " items");
}

}  // namespace

HudLayer::HudLayer() {
    // The zoom buttons never change what they are, only whether they are up.
    mZoomInButton.setImages("gallery_zoom_in", "gallery_zoom_in_touch");
    mZoomOutButton.setImages("gallery_zoom_out", "gallery_zoom_out_touch");
    mZoomInButton.setAction([this]() {
        if (mGridLayer != nullptr) {
            mGridLayer->zoomInToSelectedItem();
            mGridLayer->markDirty(1);
        }
    });
    mZoomOutButton.setAction([this]() {
        if (mGridLayer != nullptr) {
            mGridLayer->zoomOutFromSelectedItem();
            mGridLayer->markDirty(1);
        }
    });
}

void HudLayer::generate(RenderView *view, RenderLists &lists) {
    lists.updateList.push_back(this);
    // Before the bars, not after: renderBlended sets the alpha they are then
    // drawn with, so the whole HUD fades as one.
    lists.blendedList.push_back(this);
    mPathBar.generate(view, lists);
    mMenuBar.generate(view, lists);
    mFullscreenMenu.generate(view, lists);
    mSelectionMenuTop.generate(view, lists);
    mTimeBar.generate(view, lists);
    mTopRightButton.generate(view, lists);
    mZoomInButton.generate(view, lists);
    mZoomOutButton.generate(view, lists);
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

    // So does the time bar, and only one of the two is ever up.
    float timeBarHeight = TimeBar::HEIGHT * App::PIXEL_DENSITY;
    mTimeBar.setPosition(0.0f, mHeight - timeBarHeight);
    mTimeBar.setSize(mWidth, timeBarHeight);

    // And so does the fullscreen bar.
    mFullscreenMenu.setPosition(0.0f, mHeight - MenuBar::preferredHeight());
    mFullscreenMenu.setSize(mWidth, MenuBar::preferredHeight());

    // The zoom buttons stack up from the right end of that bar.
    float zoomWidth = ZOOM_BUTTON_WIDTH * App::PIXEL_DENSITY;
    float zoomHeight = ZOOM_BUTTON_HEIGHT * App::PIXEL_DENSITY;
    float zoomY = mHeight - MenuBar::preferredHeight() - zoomHeight;
    mZoomInButton.setSize(zoomWidth, zoomHeight);
    mZoomOutButton.setSize(zoomWidth, zoomHeight);
    mZoomInButton.setPosition(mWidth - zoomWidth, zoomY);
    mZoomOutButton.setPosition(mWidth - zoomWidth * 2.0f, zoomY);

    // The top selection bar takes the whole top edge, where the path bar sits
    // the rest of the time. They are never both up.
    mSelectionMenuTop.setPosition(0.0f, 0.0f);
    mSelectionMenuTop.setSize(mWidth, MenuBar::preferredHeight());

    mTopRightButton.setPosition(mWidth - TOP_RIGHT_WIDTH * App::PIXEL_DENSITY, 0.0f);
    computeBottomMenu();
}

void HudLayer::onGridStateChanged() {
    if (mGridLayer == nullptr) {
        return;
    }
    int state = mGridLayer->getState();
    if (mGridState == state) {
        return;
    }
    mGridState = state;
    // The chrome offers different things per state, so it is rebuilt here
    // rather than checked every frame.
    computeBottomMenu();
}

void HudLayer::computeBottomMenu() {
    GridLayer *grid = mGridLayer;
    if (grid == nullptr) {
        mMenuBar.clearButtons();
        mFullscreenMenu.clearButtons();
        return;
    }

    if (mMode == MODE_SELECT) {
        std::vector<MenuBar::ButtonSpec> topButtons;
        topButtons.push_back({"", "Select all", [grid]() { grid->selectAll(); }});
        // The middle one is the count. It is a button so that it takes a third
        // of the bar like the other two; it has no action.
        topButtons.push_back({"", selectionCountLabel(mNumItemsSelected), nullptr});
        topButtons.push_back({"", "Deselect all", [grid]() { grid->deselectOrCancelSelectMode(); }});
        mSelectionMenuTop.setButtons(topButtons);

        std::vector<MenuBar::ButtonSpec> buttons;
        buttons.push_back({"ic_menu_rotate_left", "", [grid]() { grid->rotateSelectedItems(-90.0f); }});
        buttons.push_back({"ic_menu_rotate_right", "", [grid]() { grid->rotateSelectedItems(90.0f); }});
        if (!grid->noDeleteMode()) {
            buttons.push_back({"icon_delete", "", [grid]() { grid->deleteSelection(); }});
        }
        mMenuBar.setButtons(buttons);
    } else {
        mMenuBar.clearButtons();
        mSelectionMenuTop.clearButtons();
    }

    if (mGridState == GridLayer::STATE_FULL_SCREEN && mMode != MODE_SELECT) {
        std::vector<MenuBar::ButtonSpec> buttons;
        // Both check the alpha first: with the chrome faded out, the first tap
        // brings it back rather than doing what the button says.
        buttons.push_back({"icon_play", "Slideshow", [this, grid]() {
                               if (getAlpha() == 1.0f) {
                                   grid->startSlideshow();
                               } else {
                                   setAlpha(1.0f);
                               }
                           }});
        buttons.push_back({"icon_more", "Menu", [this, grid]() {
                               if (getAlpha() == 1.0f) {
                                   grid->enterSelectionMode();
                               } else {
                                   setAlpha(1.0f);
                               }
                           }});
        mFullscreenMenu.setButtons(buttons);
    } else {
        mFullscreenMenu.clearButtons();
    }

    computeTopRightButton();
}

void HudLayer::computeTopRightButton() {
    GridLayer *grid = mGridLayer;
    float height = TOP_RIGHT_HEIGHT * App::PIXEL_DENSITY;
    switch (mGridState) {
    case GridLayer::STATE_GRID_VIEW:
        // Half height inside an album, so it sits over less of the wall.
        height *= 0.5f;
        mTopRightButton.setImages("mode_grid", "mode_grid");
        mTopRightButton.setAction([grid]() { grid->setState(GridLayer::STATE_TIMELINE); });
        break;
    case GridLayer::STATE_TIMELINE:
        mTopRightButton.setImages("mode_stack", "mode_stack");
        mTopRightButton.setAction([grid]() { grid->setState(GridLayer::STATE_GRID_VIEW); });
        break;
    default:
        // Over the stacks the original put a camera button here, which would
        // hand off to the camera app. There is nothing to hand off to on the
        // desktop, so the button stays away.
        mTopRightButton.setImages("", "");
        mTopRightButton.setAction(nullptr);
        break;
    }
    mTopRightButton.setSize(TOP_RIGHT_WIDTH * App::PIXEL_DENSITY, height);
}

void HudLayer::updateNumItemsSelected(int count) {
    if (mNumItemsSelected == count) {
        return;
    }
    mNumItemsSelected = count;
    // In place, not a rebuild: this runs on every tap that changes the
    // selection, and rebuilding the bar would drop the press that caused it.
    mSelectionMenuTop.setButtonLabel(1, selectionCountLabel(count));
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
    mFullscreenMenu.clearButtons();
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
    bool selectionMode = mMode == MODE_SELECT;
    // The time bar scrubs an album, so it belongs to the grid view alone. Over
    // the stacks there is nothing to scrub, and fullscreen has its own chrome.
    bool inAlbum = mGridState == GridLayer::STATE_GRID_VIEW;
    bool fullscreen = mGridState == GridLayer::STATE_FULL_SCREEN;
    mPathBar.setHidden(faded || selectionMode);
    mMenuBar.setHidden(faded || !selectionMode);
    mTimeBar.setHidden(faded || selectionMode || !inAlbum);
    mFullscreenMenu.setHidden(faded || selectionMode || !fullscreen);
    // The top bar replaces the path bar in select mode, except in fullscreen,
    // where the original left the top edge alone.
    mSelectionMenuTop.setHidden(faded || !selectionMode || fullscreen);
    // The zoom buttons belong to the fullscreen bar, and the grid takes them
    // away on its own while a photo is still settling.
    bool zoomHidden = mFullscreenMenu.isHidden() || mZoomButtonsHidden;
    mZoomInButton.setHidden(zoomHidden);
    mZoomOutButton.setHidden(zoomHidden);
    mTopRightButton.setHidden(faded || selectionMode || fullscreen);

    return mAnimAlpha != mAlpha;
}

void HudLayer::renderBlended(RenderView *view) {
    // Sets the alpha the bars below inherit. They deliberately do not reset the
    // colour, so this is what fades the whole HUD together.
    view->setAlpha(mAnimAlpha);
}
