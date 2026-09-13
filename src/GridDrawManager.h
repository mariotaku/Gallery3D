// Port of com.cooliris.media.GridDrawManager.
#pragma once

#include <vector>

#include "DisplayItem.h"
#include "DisplayList.h"
#include "DisplaySlot.h"
#include "FloatAnim.h"
#include "GestureDetector.h"
#include "GridCamera.h"
#include "GridDrawables.h"
#include "IndexRange.h"
#include "MediaBucketList.h"
#include "Texture.h"

class RenderView;

class GridDrawManager {
  public:
    static const int PASS_THUMBNAIL_CONTENT = 0;
    static const int PASS_FOCUS_CONTENT = 1;
    static const int PASS_FRAME = 2;
    static const int PASS_PLACEHOLDER = 3;
    static const int PASS_FRAME_PLACEHOLDER = 4;
    static const int PASS_TEXT_LABEL = 5;
    static const int PASS_SELECTION_LABEL = 6;
    static const int PASS_VIDEO_LABEL = 7;
    static const int PASS_LOCATION_LABEL = 8;
    static const int PASS_MEDIASET_SOURCE_LABEL = 9;

    GridDrawManager(GridCamera *camera, GridDrawables *drawables, DisplayList *displayList, DisplayItem **displayItems,
                    DisplaySlot *displaySlots);

    void prepareDraw(const IndexRange &bufferedVisibleRange, const IndexRange &visibleRange, int selectedSlot,
                     int currentFocusSlot, int currentScaleSlot, bool currentFocusIsPressed, float spreadValue,
                     ScaleGestureDetector *scaleGestureDetector, bool holdPosition);

    bool update(float timeElapsed);

    void drawThumbnails(RenderView *view, int state);
    void drawFocusItems(RenderView *view, float zoomValue, bool slideshowMode, float timeElapsedSinceView);
    void drawBlendedComponents(RenderView *view, float alpha, int state, int hudMode, float stackMixRatio,
                               float gridMixRatio, MediaBucketList &selectedBucketList,
                               MediaBucketList &markedBucketList, bool isFeedLoading);

    float getFocusQuadWidth() const {
        return mCurrentFocusItemWidth;
    }

    float getFocusQuadHeight() const {
        return mCurrentFocusItemHeight;
    }

  private:
    void drawDisplayItem(RenderView *view, DisplayItem *displayItem, const TexturePtr &texture, int pass,
                         const TexturePtr &previousTexture, float mixRatio);

    // Overlays tiles on the screennail's quad; skips items that cannot be tiled.
    void drawFocusTiles(RenderView *view, DisplayItem *displayItem, GridQuad *quad);

    static MediaItemTexture::Config sThumbnailConfig;

    DisplayItem **mDisplayItems;
    DisplaySlot *mDisplaySlots;
    DisplayList *mDisplayList;
    GridCamera *mCamera;
    GridDrawables *mDrawables;
    IndexRange mBufferedVisibleRange;
    IndexRange mVisibleRange;
    int mSelectedSlot = -1;
    int mCurrentFocusSlot = -1;
    std::vector<DisplayItem *> mItemsDrawn;
    int mDrawnCounter = 0;
    float mTargetFocusMixRatio = 0.0f;
    float mFocusMixRatio = 0.0f;
    FloatAnim mSelectedMixRatio{0.0f};
    // Which picture each fullscreen quad last drew. A quad is reused for
    // whatever is in that position, so this is what tells a shape settling on
    // one picture apart from a quad being handed the next one.
    DisplayItem *mQuadItem[3] = {nullptr, nullptr, nullptr};

    float mCurrentFocusItemWidth = 0.0f;
    float mCurrentFocusItemHeight = 0.0f;
    bool mCurrentFocusIsPressed = false;
    TexturePtr mNoItemsTexture;
    int mCurrentScaleSlot = -1;
    float mSpreadValue = 0.0f;
    ScaleGestureDetector *mScaleGestureDetector = nullptr;
    bool mHoldPosition = false;
};
