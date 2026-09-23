#include "hud/HudLayer.h"

#include "app/WindowFrame.h"

#include <SDL3/SDL.h>

#include "app/App.h"
#include "grid/GridLayer.h"
#include "media/MediaDetails.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "core/FloatUtils.h"

namespace {

// Idle time in fullscreen before the chrome gets out of the way.
const uint64_t AUTO_HIDE_MS = 5000;

// The top-right tab uses half height inside albums to clear thumbnails.
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
            mGridLayer->markDirty();
        }
    });
    mZoomOutButton.setAction([this]() {
        if (mGridLayer != nullptr) {
            mGridLayer->zoomOutFromSelectedItem();
            mGridLayer->markDirty();
        }
    });
}

void HudLayer::generate(RenderView *view, RenderLists &lists) {
    lists.updateList.push_back(this);
    // First into the hit test list, which makes it last to be asked: the bars
    // and buttons below all go in after it and so get the pointer first.
    lists.hitTestList.push_back(this);
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
    if (WindowFrame::isExtended()) {
        mCaptionButtons.generate(view, lists);
    }
    // Last, so it is on top of the bars and sees input before them: the hit
    // test walks the list backwards.
    mPopupMenu.generate(view, lists);
}

float HudLayer::selectionBarTop(float safeTop, float captionHeight) {
    // Whichever is lower: a phone's cutout, or the caption the app now draws.
    return std::max(safeTop, captionHeight);
}

float HudLayer::draggableLeft() const {
    // Past the crumbs, with a little clearance so a grab meant for the bar does
    // not land on the window instead.
    return mPathBar.barRightEdge() + 4.0f * App::UI_DENSITY;
}

float HudLayer::draggableRight() const {
    // The mode button, and the window buttons where they are drawn.
    float right = mWidth - App::SAFE_AREA.right - TOP_RIGHT_WIDTH * App::UI_DENSITY;
    if (WindowFrame::isExtended()) {
        right -= CaptionButtons::preferredWidth();
    }
    return right;
}

float HudLayer::topBarBottom() const {
    // As tall as whatever is up there: the path bar, and the caption strip when
    // the window's content runs under the title bar.
    const float inset = 3.0f * App::UI_DENSITY;
    float bottom = App::SAFE_AREA.top + inset * 2.0f + PathBarLayer::preferredHeight();
    if (WindowFrame::isExtended()) {
        bottom = std::max(bottom, WindowFrame::captionHeight());
    }
    return bottom;
}

bool HudLayer::containsPoint(float x, float y) {
    (void)x;
    // Nothing to swallow input for when the bar is not on screen. In fullscreen
    // the chrome fades out and the photo underneath wants the whole window.
    if (mAlpha <= 0.0f) {
        return false;
    }
    return y >= 0.0f && y < topBarBottom();
}

bool HudLayer::onTouchEvent(const MotionEvent &event) {
    // Consume remaining bar input after child controls; hit testing walks the list backwards.
    (void)event;
    return true;
}

void HudLayer::onPointerMoved(float x, float y) {
    if (WindowFrame::isExtended()) {
        mCaptionButtons.onPointerMoved(x, y);
    }
}

void HudLayer::onSizeChanged() {
    // Lay out HUD controls inside SDL's safe rectangle; wall and backdrop use the full window.
    const App::SafeAreaInsets &safe = App::SAFE_AREA;
    const float safeLeft = safe.left;
    const float safeTop = safe.top;
    const float safeWidth = mWidth - safe.left - safe.right;
    const float safeBottom = mHeight - safe.bottom;

    // The bar runs along the top edge. It sizes itself to its crumbs, so what
    // it needs here is the room it may use and where it starts.
    //
    // Flush to the left. Only the far end of the bar is a rounded cap; the
    // first crumb's fill runs to its own left edge with no cap at all, so the
    // art is drawn to sit against the screen. Insetting it leaves a sliver of
    // wall between that flat edge and the screen, which reads as a mistake
    // rather than a margin. The inset still applies above and at the far end.
    // Where the inset is a margin rather than an obstruction, as a TV's
    // overscan is, the bar keeps running to the screen's edge: its flat left
    // edge held a margin away reads as a bar that was cut off. Only its far end
    // and the room it may use come in.
    float inset = 3.0f * App::UI_DENSITY;
    const float barLeft = App::SAFE_AREA_IS_MARGIN ? 0.0f : safeLeft;
    mPathBar.setPosition(barLeft, safeTop + inset);
    mPathBar.setSize(safeLeft + safeWidth - inset - barLeft, PathBarLayer::preferredHeight());

    // The menu bar runs along the bottom edge.
    mMenuBar.setPosition(safeLeft, safeBottom - MenuBar::preferredHeight());
    mMenuBar.setSize(safeWidth, MenuBar::preferredHeight());

    // So does the time bar, and only one of the two is ever up.
    float timeBarHeight = TimeBar::HEIGHT * App::UI_DENSITY;
    mTimeBar.setPosition(safeLeft, safeBottom - timeBarHeight);
    mTimeBar.setSize(safeWidth, timeBarHeight);

    // And so does the fullscreen bar.
    mFullscreenMenu.setPosition(safeLeft, safeBottom - MenuBar::preferredHeight());
    mFullscreenMenu.setSize(safeWidth, MenuBar::preferredHeight());

    // The zoom buttons stack up from the right end of that bar.
    float zoomWidth = ZOOM_BUTTON_WIDTH * App::UI_DENSITY;
    float zoomHeight = ZOOM_BUTTON_HEIGHT * App::UI_DENSITY;
    float zoomY = safeBottom - MenuBar::preferredHeight() - zoomHeight;
    float safeRightEdge = safeLeft + safeWidth;
    mZoomInButton.setSize(zoomWidth, zoomHeight);
    mZoomOutButton.setSize(zoomWidth, zoomHeight);
    mZoomInButton.setPosition(safeRightEdge - zoomWidth, zoomY);
    mZoomOutButton.setPosition(safeRightEdge - zoomWidth * 2.0f, zoomY);

    // Selection replaces the path bar across the full width, below the caption buttons.
    // Those are taller than the system's caption strip, and the bar's glass
    // starts at its top edge, so it clears whichever of the two is lower.
    const float captionHeight = WindowFrame::isExtended()
                                    ? std::max(WindowFrame::captionHeight(), CaptionButtons::preferredHeight())
                                    : 0.0f;
    mSelectionMenuTop.setPosition(safeLeft, selectionBarTop(safeTop, captionHeight));
    mSelectionMenuTop.setSize(safeWidth, MenuBar::preferredHeight());

    // Place window buttons at the window's top-right corner, outside the content safe rect.
    float captionButtonsWidth = 0.0f;
    if (WindowFrame::isExtended()) {
        mCaptionButtons.setSize(CaptionButtons::preferredWidth(), CaptionButtons::preferredHeight());
        mCaptionButtons.setPosition(mWidth - CaptionButtons::preferredWidth(), 0.0f);
        captionButtonsWidth = CaptionButtons::preferredWidth();
    }

    // And the mode button steps left to make room for them. It takes the same
    // inset from the top as the path bar, so the two line up: both are drawn
    // from their top edge down, and without it the button rides a few pixels
    // high of the breadcrumb beside it.
    mTopRightButton.setPosition(safeRightEdge - TOP_RIGHT_WIDTH * App::UI_DENSITY - captionButtonsWidth,
                                safeTop + inset);
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

        // Offer delete and rotation popups only when the selection's sources support writes.
        std::vector<MenuBar::ButtonSpec> buttons;
        if (!grid->noDeleteMode()) {
            size_t index = buttons.size();
            buttons.push_back({"icon_delete", "Delete", [this, grid, index]() {
                                   // Confirm deletion before moving files to the recycle bin.
                                   showPopupFor(mMenuBar, index,
                                                {{"Confirm delete", "icon_delete",
                                                  [grid]() { grid->deleteSelection(); }},
                                                 {"Cancel", "icon_cancel", nullptr}});
                               }});
        }
        // Details uses loaded metadata and remains available for read-only sources.
        {
            const bool canRotate = grid->selectionSupports(MediaFeed::OPERATION_ROTATE);
            size_t moreIndex = buttons.size();
            buttons.push_back({"icon_more", "More", [this, grid, moreIndex, canRotate]() {
                                   std::vector<PopupMenu::Option> options;
                                   if (canRotate) {
                                       options.push_back({"Rotate left", "ic_menu_rotate_left",
                                                          [grid]() { grid->rotateSelectedItems(-90.0f); }});
                                       options.push_back({"Rotate right", "ic_menu_rotate_right",
                                                          [grid]() { grid->rotateSelectedItems(90.0f); }});
                                   }
                                   options.push_back({"Details", "ic_menu_view_details",
                                                      [this, moreIndex]() { showDetails(mMenuBar, moreIndex); }});
                                   showPopupFor(mMenuBar, moreIndex, options);
                               }});
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
        buttons.push_back({"icon_more", "More", [this]() {
                               if (getAlpha() == 1.0f) {
                                   showFullscreenMore(1);
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
    float height = TOP_RIGHT_HEIGHT * App::UI_DENSITY;
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
        // No desktop camera-app button.
        mTopRightButton.setImages("", "");
        mTopRightButton.setAction(nullptr);
        break;
    }
    mTopRightButton.setSize(TOP_RIGHT_WIDTH * App::UI_DENSITY, height);
}

void HudLayer::showPopupFor(const MenuBar &bar, size_t index, const std::vector<PopupMenu::Option> &options) {
    mPopupMenu.setOptions(options);
    // Anchored to the top of the bar the button is in, so the popup sits above
    // it and its triangle points down at the button.
    mPopupMenu.showAtPoint(bar.buttonCenterX(index), bar.getY(), App::SAFE_AREA.left,
                           mWidth - App::SAFE_AREA.left - App::SAFE_AREA.right);
}

void HudLayer::showFullscreenMore(size_t buttonIndex) {
    GridLayer *grid = mGridLayer;
    // The actions work on the selection. The photo on screen is the selection
    // only while an action runs, so selection mode entered later starts empty.
    if (grid == nullptr || !grid->selectOnlyCurrentItem()) {
        return;
    }
    const bool canDelete = !grid->noDeleteMode();
    const bool canRotate = grid->selectionSupports(MediaFeed::OPERATION_ROTATE);
    grid->clearSelectedItems();

    std::vector<PopupMenu::Option> options;
    if (canDelete) {
        options.push_back({"Delete", "icon_delete", [this, grid, buttonIndex]() {
                               showPopupFor(mFullscreenMenu, buttonIndex,
                                            {{"Confirm delete", "icon_delete",
                                              [grid]() {
                                                  if (grid->selectOnlyCurrentItem()) {
                                                      grid->deleteSelection();
                                                  }
                                              }},
                                             {"Cancel", "icon_cancel", nullptr}});
                           }});
    }
    if (canRotate) {
        for (float degrees : {-90.0f, 90.0f}) {
            options.push_back({degrees < 0.0f ? "Rotate left" : "Rotate right",
                               degrees < 0.0f ? "ic_menu_rotate_left" : "ic_menu_rotate_right",
                               [grid, degrees]() {
                                   if (grid->selectOnlyCurrentItem()) {
                                       grid->rotateSelectedItems(degrees);
                                       grid->clearSelectedItems();
                                   }
                               }});
        }
    }
    options.push_back({"Details", "ic_menu_view_details", [this, grid, buttonIndex]() {
                           if (grid->selectOnlyCurrentItem()) {
                               showDetails(mFullscreenMenu, buttonIndex);
                               grid->clearSelectedItems();
                           }
                       }});
    showPopupFor(mFullscreenMenu, buttonIndex, options);
}

bool HudLayer::showSourceMenu() {
    if (!mSourceMenu) {
        return false;
    }
    std::vector<PopupMenu::Option> options = mSourceMenu();
    if (options.empty()) {
        return false;
    }
    mPopupMenu.setOptions(options);
    mPopupMenu.showBelowPoint(mPathBar.crumbCenterX(0), mPathBar.getY() + PathBarLayer::preferredHeight(),
                              App::SAFE_AREA.left, mWidth - App::SAFE_AREA.left - App::SAFE_AREA.right);
    return true;
}

void HudLayer::setHomeLabel(const std::string &label) {
    mPathBar.changeLabelAt(0, label);
}

void HudLayer::showDetails(const MenuBar &bar, size_t buttonIndex) {
    std::vector<PopupMenu::Option> options;
    for (const std::string &line : MediaDetails::linesFor(mGridLayer->getSelectedBucketList())) {
        options.push_back({line, "", nullptr});
    }
    if (options.empty()) {
        return;
    }
    options.push_back({"OK", "", nullptr});
    showPopupFor(bar, buttonIndex, options);
}

void HudLayer::closeSelectionMenu() {
    mPopupMenu.close(true);
}

void HudLayer::fullscreenSelectionChanged(MediaItem *item, int index, int count) {
    if (item == nullptr) {
        return;
    }
    mCachedPosition = std::to_string(index) + "/" + std::to_string(count);
    mCachedCaption = item->mCaption;
    // Opens on the position rather than the caption, which is what tells you
    // where you are in the album.
    mCachedCurrentLabel = mCachedPosition;
    mPathBar.changeLabel(mCachedCurrentLabel);
}

void HudLayer::swapFullscreenLabel() {
    bool showingCaption = !mCachedCaption.empty() && mCachedCurrentLabel == mCachedCaption;
    mCachedCurrentLabel = (showingCaption || mCachedCaption.empty()) ? mCachedPosition : mCachedCaption;
    mPathBar.changeLabel(mCachedCurrentLabel);
}

void HudLayer::updateNumItemsSelected(int count) {
    // What the selection can have done to it changes with what is in it, so the
    // bar is rebuilt when that flips rather than only when the mode does.
    if (mGridLayer != nullptr && mMode == MODE_SELECT) {
        bool canDelete = !mGridLayer->noDeleteMode();
        bool canRotate = mGridLayer->selectionSupports(MediaFeed::OPERATION_ROTATE);
        if (canDelete != mSelectionCanDelete || canRotate != mSelectionCanRotate) {
            mSelectionCanDelete = canDelete;
            mSelectionCanRotate = canRotate;
            mNumItemsSelected = count;
            computeBottomMenu();
            return;
        }
    }
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
    mPopupMenu.close(false);
    setAlpha(1.0f);
    mAnimAlpha = 1.0f;
}

void HudLayer::enterSelectionMode() {
    setAlpha(1.0f);
    setMode(MODE_SELECT);
}

void HudLayer::cancelSelection() {
    // The popup came from the selection bar, which leaves with the selection.
    mPopupMenu.close(true);
    setMode(MODE_NORMAL);
}

bool HudLayer::update(RenderView *view, float frameInterval) {
    (void)view;
    // Four times faster on the way in than on the way out, so the chrome
    // answers a tap immediately but leaves gently.
    float factor = (mAlpha == 1.0f) ? 4.0f : 1.0f;
    mAnimAlpha = FloatUtils::animate(mAnimAlpha, mAlpha, frameInterval * factor);

    // An open popup is being read, so the chrome it hangs off stays up, and
    // the idle clock starts again when it closes.
    if (mPopupMenu.isShowing() && mAlpha == 1.0f) {
        mLastTimeFullOpacity = SDL_GetTicks();
    }

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
    // A popup outlives the bar it came from only long enough to fade, and the
    // chrome fading out takes it with it.
    if (faded && mPopupMenu.isShowing()) {
        mPopupMenu.close(true);
    }

    return mAnimAlpha != mAlpha;
}

void HudLayer::renderBlended(RenderView *view) {
    // Sets the alpha the bars below inherit. They deliberately do not reset the
    // colour, so this is what fades the whole HUD together.
    view->setAlpha(mAnimAlpha);
}
