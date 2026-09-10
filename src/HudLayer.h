// Partial port of com.cooliris.media.HudLayer and the bars it owns.
//
// The layer itself is real now: it animates its own opacity, hides itself after
// five idle seconds in fullscreen, and switches between the normal and select
// modes. The path bar is real. The menu bar, the selection menu and the time
// bar are still stubs that exist so the grid's call sites compile.
#pragma once

#include <cstdint>
#include <string>

#include "Layer.h"
#include "MenuBar.h"
#include "PathBarLayer.h"
#include "RenderView.h"

class MediaItem;
class MediaFeed;
class GridLayer;

class TimeBar {
  public:
    class Listener {
      public:
        virtual ~Listener() = default;
        virtual void onTimeChanged(TimeBar *timebar) = 0;
    };

    void setListener(Listener *listener) {
        mListener = listener;
    }

    void setItem(MediaItem *item) {
        mItem = item;
    }

    MediaItem *getItem() const {
        return mItem;
    }

    bool isDragged() const {
        return false;
    }

  private:
    Listener *mListener = nullptr;
    MediaItem *mItem = nullptr;
};

class HudLayer : public Layer {
  public:
    static const int MODE_NORMAL = 0;
    static const int MODE_SELECT = 1;

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
    void onGridStateChanged() {}

    // Only fullscreen asks for this. Everywhere else the grid pins the alpha
    // to 1 every frame, which keeps the idle timer from ever expiring.
    void autoHide(bool enable) {
        mAutoHide = enable;
    }

    void enterSelectionMode();
    void cancelSelection();
    void closeSelectionMenu() {}
    // Rebuilds the bottom bar for the current mode.
    void computeBottomMenu();
    void updateNumItemsSelected(int count) {
        (void)count;
    }
    void fullscreenSelectionChanged(MediaItem *item, int index, int count) {
        (void)item;
        (void)index;
        (void)count;
    }
    void setFeed(MediaFeed *feed, int state, bool needsLayout) {
        (void)feed;
        (void)state;
        (void)needsLayout;
    }
    void setTimeBarTime(int64_t time) {
        (void)time;
    }
    void swapFullscreenLabel() {}
    void hideZoomButtons(bool hide) {
        (void)hide;
    }

  protected:
    void onSizeChanged() override;

  private:
    GridLayer *mGridLayer = nullptr;
    PathBarLayer mPathBar;
    TimeBar mTimeBar;
    MenuBar mMenuBar;
    float mAlpha = 1.0f;
    float mAnimAlpha = 1.0f;
    bool mAutoHide = false;
    uint64_t mLastTimeFullOpacity = 0;
    int mMode = MODE_NORMAL;
};
