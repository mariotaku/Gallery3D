// Port of com.cooliris.media.GridLayer.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "BackgroundLayer.h"
#include "DisplayItem.h"
#include "DisplayList.h"
#include "DisplaySlot.h"
#include "GridCamera.h"
#include "GridCameraManager.h"
#include "GridDrawManager.h"
#include "GridDrawables.h"
#include "GridInputProcessor.h"
#include "GridLayoutInterface.h"
#include "HudLayer.h"
#include "LoadingLayer.h"
#include "IndexRange.h"
#include "Layer.h"
#include "MediaBucketList.h"
#include "MediaFeed.h"

class DataSource;
class RenderView;

class GridLayer : public RootLayer, public MediaFeed::Listener, public TimeBar::Listener {
  public:
    static const int STATE_MEDIA_SETS = 0;
    static const int STATE_GRID_VIEW = 1;
    static const int STATE_FULL_SCREEN = 2;
    static const int STATE_TIMELINE = 3;

    static const int ANCHOR_LEFT = 0;
    static const int ANCHOR_RIGHT = 1;
    static const int ANCHOR_CENTER = 2;

    static const int MAX_ITEMS_PER_SLOT = 32;
    static const int MAX_DISPLAYED_ITEMS_PER_SLOT = 4;
    static const int MAX_DISPLAYED_ITEMS_PER_FOCUSED_SLOT = 32;
    static const int MAX_DISPLAY_SLOTS = 96;
    static const int MAX_ITEMS_DRAWABLE = MAX_ITEMS_PER_SLOT * MAX_DISPLAY_SLOTS;

    // The wall's cell, in the units the original was laid out in, scaled by
    // the wall's density. One place, because onDensityChanged has to work them
    // out again and the two answers have to agree.
    static int itemWidthForDensity();
    static int itemHeightForDensity();

    GridLayer(int itemWidth, int itemHeight, LayoutInterface *layoutInterface, RenderView *view);

    // Rebuilds everything whose size was fixed at the old density: the cell,
    // the slot spacing, the shared quads and every drawable, which is reloaded
    // from whichever bucket the new density asks for. The caller follows this
    // with RenderView::onSurfaceChanged to put the new sizes through the
    // layout.
    void onDensityChanged();
    ~GridLayer() override;

    HudLayer *getHud() {
        return &mHud;
    }

    void shutdown();
    void stop();

    // Layer
    void generate(RenderView *view, RenderLists &lists) override;
    bool update(RenderView *view, float timeElapsed) override;
    void renderOpaque(RenderView *view) override;
    void renderBlended(RenderView *view) override;
    bool onTouchEvent(const MotionEvent &event) override;
    void onSurfaceCreated(RenderView *view) override;

    // RootLayer
    bool onKeyDown(int keyCode, const KeyEvent &event) override;
    void onSurfaceChanged(RenderView *view, int width, int height) override;
    void handleLowMemory() override;

    // MediaFeed::Listener
    void onFeedAboutToChange(MediaFeed *feed) override;
    void onFeedChanged(MediaFeed *feed, bool needsLayout) override;

    // TimeBar::Listener
    void onTimeChanged(TimeBar *timebar) override;

    int getState() const {
        return mState;
    }
    void setState(int state);

    DataSource *getDataSource();
    void setDataSource(DataSource *dataSource);

    const IndexRange &getVisibleRange() const {
        return mVisibleRange;
    }
    const IndexRange &getBufferedVisibleRange() const {
        return mBufferedVisibleRange;
    }
    const IndexRange &getCompleteRange() const {
        return mCompleteRange;
    }

    void centerCameraForSlot(int slotIndex, float baseConvergence);
    bool constrainCameraForSlot(int slotIndex);

    bool goBack();
    void endSlideshow();
    void startSlideshow();

    DisplayItem *getRepresentativeDisplayItem();
    DisplayItem *getAnchorDisplayItem(int type);
    float getScrollPosition() const;
    DisplayItem *getDisplayItemForScrollPosition(float posX);
    int getAnchorSlotIndex(int anchorType) const;
    DisplayItem *getDisplayItemForSlotId(int slotId);

    bool changeFocusToNextSlot(float convergence);
    bool changeFocusToSlot(int slotId, float convergence);
    bool changeFocusToPreviousSlot(float convergence);

    std::vector<MediaBucket> &getSelectedBuckets();
    void selectAll();
    void deselectOrCancelSelectMode();
    void deselectAll();
    void deleteSelection();
    void addSlotToSelectedItems(int slotId, bool removeIfAlreadyAdded, bool updateCount);

    int getMetadataSlotIndexForScreenPosition(int posX, int posY);
    int getSlotIndexForScreenPosition(int posX, int posY);

    bool tapGesture(int slotIndex, bool metadata);

    void enterSelectionMode();
    void zoomInToSelectedItem();
    void zoomOutFromSelectedItem();
    void rotateSelectedItems(float f);

    bool inSlideShowMode() const {
        return mSlideshowMode;
    }
    bool noDeleteMode() const;
    // Whether the sources behind the current selection can do this at all. The
    // HUD asks before it offers the button.
    bool selectionSupports(int operation) const;

    float getZoomValue() const {
        return mZoomValue;
    }
    void setZoomValue(float f);

    bool feedAboutToChange() const {
        return mFeedAboutToChange;
    }

    bool isInAlbumMode() const {
        return mInAlbum;
    }

    const Vector3f &getDeltaAnchorPosition() const {
        return mDeltaAnchorPosition;
    }

    int getExpandedSlot() const {
        return mCurrentExpandedSlot;
    }

    GridLayoutInterface *getLayoutInterface() {
        return (GridLayoutInterface *)mLayoutInterface;
    }

    bool getPickIntent() const {
        return mPickIntent;
    }

    bool getViewIntent() const {
        return mViewIntent;
    }

    MediaFeed *getFeed() {
        return mMediaFeed.get();
    }

    void markDirty(int numFrames) {
        mFramesDirty = numFrames;
    }

    void onLayout(int newAnchorSlotIndex, int currentAnchorSlotIndex, LayoutInterface *oldLayout);

    GridInputProcessor *getInputProcessor() {
        return mInputProcessor.get();
    }

  protected:
    void onSizeChanged() override;

    // Empties the display list and forgets the per slot pointers into it, which
    // the list owns and has just destroyed.
    void clearDisplayList();


  private:
    int hitTest(const Vector3f &worldPos, int itemWidth, int itemHeight);
    int getSlotForScreenPosition(int posX, int posY, int itemWidth, int itemHeight);
    void computeVisibleRange();
    void computeVisibleItems();
    void forceRecomputeVisibleRange();
    void clearUnusedThumbnails();
    void updateCountOfSelectedItems();
    void enableLocationFiltering(const std::string &label);
    void disableLocationFiltering();
    float getFillScreenZoomValue();

    HudLayer mHud;
    int mState = STATE_MEDIA_SETS;
    IndexRange mBufferedVisibleRange;
    IndexRange mVisibleRange;
    IndexRange mPreviousDataRange;
    IndexRange mCompleteRange;

    Vector3f mDeltaAnchorPositionUncommited;
    Vector3f mDeltaAnchorPosition;

    std::unique_ptr<GridDrawables> mDrawables;
    float mSelectedAlpha = 0.0f;
    float mTargetAlpha = 0.0f;

    std::unique_ptr<GridCamera> mCamera;
    std::unique_ptr<GridCameraManager> mCameraManager;
    std::unique_ptr<GridDrawManager> mDrawManager;
    std::unique_ptr<GridInputProcessor> mInputProcessor;

    bool mFeedAboutToChange = false;
    bool mPerformingLayoutChange = false;
    bool mFeedChanged = false;

    LayoutInterface *mLayoutInterface;
    static GridLayoutInterface sFullScreenLayoutInterface;

    std::unique_ptr<MediaFeed> mMediaFeed;
    bool mInAlbum = false;
    int mCurrentExpandedSlot = -1;

    DisplayList mDisplayList;
    DisplayItem *mDisplayItems[MAX_ITEMS_DRAWABLE] = {nullptr};
    DisplaySlot mDisplaySlots[MAX_DISPLAY_SLOTS];
    std::vector<MediaItem *> mVisibleItems;

    BackgroundLayer mBackground;
    LoadingLayer mLoading;
    bool mLocationFilter = false;
    float mZoomValue = 1.0f;
    float mCurrentFocusItemWidth = 1.0f;
    float mCurrentFocusItemHeight = 1.0f;
    float mTimeElapsedSinceGridViewReady = 0.0f;

    bool mSlideshowMode = false;
    bool mNoDeleteMode = false;
    float mTimeElapsedSinceView = 0.0f;
    MediaBucketList mSelectedBucketList;
    MediaBucketList mMarkedBucketList;
    float mTimeElapsedSinceStackViewReady = 0.0f;

    RenderView *mView;
    bool mPickIntent = false;
    bool mViewIntent = false;
    int mStartMemoryRange = 0;
    int mFramesDirty = 0;
    std::string mRequestFocusContentUri;
    int mFrameCount = 0;
    bool mRequestToEnterSelection = false;
};
