// Port of com.cooliris.media.GridDrawManager.
#pragma once

#include <vector>

#include "grid/DisplayItem.h"
#include "grid/DisplayList.h"
#include "grid/DisplaySlot.h"
#include "core/FloatAnim.h"
#include "grid/GestureDetector.h"
#include "grid/GridCamera.h"
#include "grid/GridDrawables.h"
#include "core/IndexRange.h"
#include "media/MediaBucketList.h"
#include "graphics/Texture.h"

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
                     int currentFocusSlot, int currentScaleSlot, bool currentFocusIsPressed, int hoverSlot,
                     float spreadValue, ScaleGestureDetector *scaleGestureDetector, bool holdPosition);

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
    // The item's grid thumbnail, or the broken picture once that thumbnail
    // has failed to decode. The broken picture is loaded with the frames, so
    // an item that failed is drawn and framed like any other.
    TexturePtr thumbnailOf(DisplayItem *displayItem) const;

    void drawDisplayItem(RenderView *view, DisplayItem *displayItem, const TexturePtr &texture, int pass,
                         const TexturePtr &previousTexture, float mixRatio);

    // Overlays tiles on the screennail's quad; skips items that cannot be tiled.
    void drawFocusTiles(RenderView *view, DisplayItem *displayItem, GridQuad *quad);

    // Draws the checkerboard at the quad's current shape, with cells a fixed
    // size on screen, then puts the quad back.
    void drawFocusChecker(RenderView *view, DisplayItem *displayItem, GridQuad *quad, float offsetX, float offsetY,
                          float u, float v, float alpha);

    // The shadows around the pictures drawFocusItems drew this frame. Runs in
    // the blended pass, after the backdrop it darkens.
    void drawFocusShadows(RenderView *view, float visibility);

    // Where a fullscreen picture was drawn, in its local space, for its shadow.
    struct FocusShadow {
        DisplayItem *item = nullptr;
        float width = 0.0f;
        float height = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float alpha = 0.0f;
    };

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
    float mCurrentFocusItemWidth = 0.0f;
    float mCurrentFocusItemHeight = 0.0f;
    FocusShadow mFocusShadows[3];
    bool mCurrentFocusIsPressed = false;
    // The slot under a mouse that is only passing over, or -1.
    int mHoverSlot = -1;
    TexturePtr mNoItemsTexture;
    int mCurrentScaleSlot = -1;
    float mSpreadValue = 0.0f;
    ScaleGestureDetector *mScaleGestureDetector = nullptr;
    bool mHoldPosition = false;
};
