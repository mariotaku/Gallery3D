// Partial port of com.cooliris.media.HudLayer and the bars it owns.
//
// The path bar is real now. The rest - the menu bar, the selection menu, the
// time bar - is still a stub that exists so the grid's call sites compile: it
// draws nothing, always reports MODE_NORMAL and full alpha. The grid drives the
// HUD from a dozen places, so the surface is kept and the behaviour is not.
#pragma once

#include <cstdint>
#include <string>

#include "Layer.h"
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

class MenuBar {
  public:
    void setHidden(bool hidden) {
        mHidden = hidden;
    }

  private:
    bool mHidden = false;
};

class HudLayer : public Layer {
  public:
    static const int MODE_NORMAL = 0;
    static const int MODE_SELECT = 1;

    void generate(RenderView *view, RenderLists &lists) override;

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

    float getAlpha() const {
        return mAlpha;
    }

    void setAlpha(float alpha) {
        mAlpha = alpha;
    }

    int getMode() const {
        return mMode;
    }

    void setMode(int mode) {
        mMode = mode;
    }

    void clear() {}
    void reset() {}
    void onGridStateChanged() {}
    void autoHide(bool enable) {
        (void)enable;
    }
    void enterSelectionMode() {}
    void cancelSelection() {}
    void closeSelectionMenu() {}
    void computeBottomMenu() {}
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
    int mMode = MODE_NORMAL;
};
