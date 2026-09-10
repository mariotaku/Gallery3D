// Partial port of com.cooliris.media.HudLayer and the bars it owns.
//
// The layer itself is real now: it animates its own opacity, hides itself after
// five idle seconds in fullscreen, and switches between the normal and select
// modes. The path bar, the menu bar and the time bar are real. The selection
// menu and the zoom buttons are still stubs that exist so the grid's call sites
// compile.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ImageButton.h"
#include "Layer.h"
#include "MenuBar.h"
#include "PathBarLayer.h"
#include "PopupMenu.h"
#include "RenderView.h"
#include "TimeBar.h"

class MediaItem;
class MediaFeed;
class GridLayer;

class HudLayer : public Layer {
  public:
    static const int MODE_NORMAL = 0;
    static const int MODE_SELECT = 1;

    HudLayer();

    void generate(RenderView *view, RenderLists &lists) override;
    bool update(RenderView *view, float frameInterval) override;
    void renderBlended(RenderView *view) override;

    void setGridLayer(GridLayer *layer) {
        mGridLayer = layer;
    }

    PathBarLayer *getPathBar() {
        return &mPathBar;
    }

    TimeBar *getTimeBar() {
        return &mTimeBar;
    }

    MenuBar *getMenuBar() {
        return &mMenuBar;
    }

    MenuBar *getFullscreenMenu() {
        return &mFullscreenMenu;
    }

    MenuBar *getSelectionMenuTop() {
        return &mSelectionMenuTop;
    }

    // The target. What is actually drawn animates toward it.
    float getAlpha() const {
        return mAlpha;
    }

    void setAlpha(float alpha);

    int getMode() const {
        return mMode;
    }

    void setMode(int mode);

    void clear() {}
    void reset();
    void onGridStateChanged();

    // Only fullscreen asks for this. Everywhere else the grid pins the alpha
    // to 1 every frame, which keeps the idle timer from ever expiring.
    void autoHide(bool enable) {
        mAutoHide = enable;
    }

    void enterSelectionMode();
    void cancelSelection();
    void closeSelectionMenu();
    // Rebuilds the bars and the top right button for the current mode and grid
    // state. Both change what the chrome offers, so both come through here.
    void computeBottomMenu();
    // The one button that changes with every grid state.
    void computeTopRightButton();
    // Opens the popup above a bar button, pointing back at it.
    void showPopupFor(const MenuBar &bar, size_t index, const std::vector<PopupMenu::Option> &options);
    void updateNumItemsSelected(int count);
    void fullscreenSelectionChanged(MediaItem *item, int index, int count) {
        (void)item;
        (void)index;
        (void)count;
    }
    void setFeed(MediaFeed *feed, int state, bool needsLayout) {
        mTimeBar.setFeed(feed, state, needsLayout);
    }
    void setTimeBarTime(int64_t time) {
        (void)time;
    }
    void swapFullscreenLabel() {}
    void hideZoomButtons(bool hide) {
        mZoomButtonsHidden = hide;
    }

  protected:
    void onSizeChanged() override;

  private:
    GridLayer *mGridLayer = nullptr;
    PathBarLayer mPathBar;
    TimeBar mTimeBar;
    MenuBar mMenuBar;
    // Fullscreen gets its own bar, because it offers different things and is up
    // at the same time as nothing else.
    MenuBar mFullscreenMenu;
    // Select mode gets a second bar along the top: select all, the count, and
    // deselect all. The count is the middle button, which is why it has one.
    MenuBar mSelectionMenuTop;
    // One popup, reused. Only ever one is open.
    PopupMenu mPopupMenu;
    ImageButton mTopRightButton;
    ImageButton mZoomInButton;
    ImageButton mZoomOutButton;
    bool mZoomButtonsHidden = false;
    int mNumItemsSelected = 0;
    // The grid state the bars follow. Only the time bar cares: it belongs to
    // the album view and to nothing else.
    int mGridState = 0;
    float mAlpha = 1.0f;
    float mAnimAlpha = 1.0f;
    bool mAutoHide = false;
    uint64_t mLastTimeFullOpacity = 0;
    int mMode = MODE_NORMAL;
};
