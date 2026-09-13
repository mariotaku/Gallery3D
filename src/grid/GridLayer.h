// Port of com.cooliris.media.GridLayer.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "grid/BackgroundLayer.h"
#include "grid/DisplayItem.h"
#include "grid/DisplayList.h"
#include "grid/DisplaySlot.h"
#include "grid/GridCamera.h"
#include "grid/GridCameraManager.h"
#include "grid/GridDrawManager.h"
#include "grid/GridDrawables.h"
#include "grid/GridInputProcessor.h"
#include "grid/GridLayoutInterface.h"
#include "hud/HudLayer.h"
#include "hud/LoadingLayer.h"
#include "core/IndexRange.h"
#include "graphics/Layer.h"
#include "media/MediaBucketList.h"
#include "media/MediaFeed.h"

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
    // Reserve array capacity for GridCameraManager's 24-slot range padding.
    static const int kSlotRangePadding = 40;
    static const int MAX_ITEMS_DRAWABLE = MAX_ITEMS_PER_SLOT * MAX_DISPLAY_SLOTS;

    // Wall cell dimensions scaled by wall density; reused on density changes.
    static int itemWidthForDensity();
    static int itemHeightForDensity();

    // How many rows of slots the wall should use, for the window it is in and
    // the set it is showing. At least one, however small the window, and never
    // more than the display slot array can hold.
    int rowsForViewport(int spacingX, int spacingY) const;

    // Puts that on the layout, for the state and feed as they are now. Called
    // whenever either changes.
    // Takes the state being laid out for. setState assigns mState after its
    // switch, so a caller inside that switch would otherwise be asking about
    // the state being left.
    void updateRowsForLayout(int forState);

    // Keeps the camera inside the range it may scroll over, which for a wall
    // that fits the window is a single point in the middle of it.
    void keepWallInRange();

    // Clear entries not refilled this pass before DisplayList can destroy their items.
    void clearDisplayItems(int begin, int end);

    GridLayer(int itemWidth, int itemHeight, LayoutInterface *layoutInterface, RenderView *view);

    // Rebuild density-dependent cells, spacing, quads and drawable caches.
    // The caller then invokes RenderView::onSurfaceChanged to relayout.
    void onDensityChanged();

    void onPointerMoved(float x, float y) override;
    void onAccelerometer(float x, float y, float z) override;
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

    // Which slot the blurred background belongs to. Static and given every
    // input, so the rule can be checked without a layer around it.
    static int representativeSlotIndex(int state, int focusSlot, int selectedSlot, int anchorCenterSlot);

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

    // What is selected, for anything that only reads it.
    const MediaBucketList &getSelectedBucketList() const {
        return mSelectedBucketList;
    }

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

    // Draw one more frame, for a change that has already been made.
    void markDirty() {
        mDirtyFrames = 1;
    }

    // Keep drawing for this long, for something that settles over time. In
    // seconds rather than frames: a frame is half as long on a 120Hz panel, so
    // a count of them would run out in half the time it was meant to.
    void markDirtyFor(float seconds) {
        if (seconds > mDirtySeconds) {
            mDirtySeconds = seconds;
        }
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

    // Asks the feed for the next page when the wall is running out of loaded
    // items to show.
    void requestMoreItemsIfNearTheEnd();


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
    // Where the mouse last was, for the hover effect. Negative once it has
    // left the window.
    float mPointerX = -1.0f;
    float mPointerY = -1.0f;
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
    int mDirtyFrames = 0;
    float mDirtySeconds = 0.0f;
    std::string mRequestFocusContentUri;
    int mFrameCount = 0;
    bool mRequestToEnterSelection = false;
};
