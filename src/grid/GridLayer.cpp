#include "grid/GridLayer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "app/App.h"
#include "core/FloatUtils.h"
#include "media/LocalDataSource.h"
#include "hud/MenuBar.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "hud/PathBarLayer.h"
#include "graphics/RenderView.h"
#include "core/Shared.h"

static const float SLIDESHOW_TRANSITION_TIME = 3.5f;

GridLayoutInterface GridLayer::sFullScreenLayoutInterface(1);

namespace {

// ArrayUtils.computeSortedIntersection using pointer identity.
void computeSortedIntersection(const std::vector<MediaItem *> &firstList, const std::vector<MediaItem *> &secondList,
                               int maxSize, std::vector<MediaItem *> &intersectionList) {
    for (MediaItem *item : firstList) {
        if (item == nullptr) {
            continue;
        }
        if (std::find(secondList.begin(), secondList.end(), item) != secondList.end()) {
            intersectionList.push_back(item);
            if (--maxSize == 0) {
                break;
            }
        }
    }
}

bool contains(const std::vector<MediaItem *> &items, MediaItem *item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

// The least the grid leaves above and below itself, as a share of the window's
// height. A proportion rather than a fixed size because this is about how the
// screen is divided up, which is the same question on a small screen as on a
// large one. A fixed size is not: it is a fifth of a short window and a
// twentieth of a tall one, so it costs a short screen rows it cannot spare.
const float kGridMarginFraction = 0.05f;

// Below this the grid reads as a column rather than a wall. A screen short
// enough that the margin costs it this much keeps the rows instead: the
// proportion shrinks with the screen, but on a small one at a high density
// barely two rows fit before any margin is taken.
const int kGridMinRows = 2;

// The air between rows of stacks, before the aspect stretch. Wider than it
// looks: the stack art fans out past its cell at both ends, and the album's
// label hangs below that again. Closing it far enough for a fourth row on a
// handset leaves each label sitting on the stack beneath it.
const float kStackSpacingY = 70.0f;

}  // namespace

int GridLayer::itemWidthForDensity() {
    return (int)(96.0f * App::PIXEL_DENSITY);
}

int GridLayer::itemHeightForDensity() {
    return (int)(72.0f * App::PIXEL_DENSITY);
}

int GridLayer::rowsForViewport(int spacingX, int spacingY) const {
    const int pitch = mCamera->mItemHeight + spacingY;
    if (pitch <= 0) {
        return 1;
    }

    // Lay out rows between the top and bottom bars; the wall still draws edge to edge.
    const App::SafeAreaInsets &safe = App::SAFE_AREA;
    const float betweenBars = (float)mCamera->mHeight - safe.top - safe.bottom -
                              PathBarLayer::preferredHeight() - MenuBar::preferredHeight();

    // Rows are chosen by what is left over rather than by what fits. The block
    // is centred, so half the leftover shows at each end, and a row only earns
    // its place if both ends keep this much.
    const float minMargin = kGridMarginFraction * (float)mCamera->mHeight;
    int available = (int)(betweenBars - 2.0f * minMargin);

    // n rows span n items and the n-1 gaps between them.
    int rows = (available + spacingY) / pitch;

    // The air is what gives way when there is not enough of both.
    if (rows < kGridMinRows) {
        available = (int)betweenBars;
        rows = (available + spacingY) / pitch;
    }

    const int columnPitch = mCamera->mItemWidth + spacingX;
    if (columnPitch <= 0) {
        return (rows < 1) ? 1 : rows;
    }

    // Reserve display-array entries for visible columns and buffered slots on either side.
    const int columnsOnScreen = mCamera->mWidth / columnPitch;
    const int maxRows = (MAX_DISPLAY_SLOTS - kSlotRangePadding) / (columnsOnScreen + 2);
    if (rows > maxRows) {
        rows = maxRows;
    }
    if (rows < 1) {
        rows = 1;
    }

    // For scrolling walls, fill the height. For walls that fit, choose the row count
    // whose block aspect ratio is closest to the window's.
    const int slots = (mMediaFeed != nullptr) ? mMediaFeed->getNumSlots() : 0;
    if (slots <= 0 || available <= 0) {
        return rows;
    }
    const float windowAspect = (float)mCamera->mWidth / (float)available;
    float bestError = -1.0f;
    int bestRows = rows;
    for (int candidate = 1; candidate <= rows; ++candidate) {
        const int columns = (slots + candidate - 1) / candidate;
        if (columns > columnsOnScreen) {
            // Wider than the window, so this many rows does not make a block
            // that can be seen at once.
            continue;
        }
        const float blockAspect = (float)(columns * columnPitch) / (float)(candidate * pitch);
        // Compared as a ratio rather than a difference, so being twice as wide
        // and half as wide count the same.
        float error = blockAspect / windowAspect;
        if (error < 1.0f) {
            error = 1.0f / error;
        }
        if (bestError < 0.0f || error < bestError) {
            bestError = error;
            bestRows = candidate;
        }
    }
    return bestRows;
}

void GridLayer::keepWallInRange() {
    // Fullscreen moves the camera photo by photo, and a slot being zoomed into
    // owns the camera outright. Neither is scrolling the wall.
    if (mState == STATE_FULL_SCREEN || mInputProcessor == nullptr ||
        mInputProcessor->getCurrentSelectedSlot() != Shared::INVALID) {
        return;
    }
    // The feed rather than mCompleteRange, which keeps the previous feed's
    // slot count until the next draw. Leaving an album for the stacks would
    // otherwise clamp the camera to the album's few slots.
    const int numSlots = mMediaFeed ? mMediaFeed->getNumSlots() : 0;
    if (numSlots <= 0) {
        return;
    }
    Vector3f firstPosition;
    Vector3f lastPosition;
    // The anchor onLayout has just set rather than the one the draw last
    // committed. Leaving fullscreen centres the camera on the photo with the
    // new anchor, while the committed one still places the wall where the
    // fullscreen row had it, and clamping to that range throws the camera off
    // the photo.
    Vector3f deltaAnchorPosition(mDeltaAnchorPositionUncommited);
    GridCameraManager::getSlotPositionForSlotIndex(0, mCamera.get(), mLayoutInterface, deltaAnchorPosition,
                                                  firstPosition);
    GridCameraManager::getSlotPositionForSlotIndex(numSlots - 1, mCamera.get(), mLayoutInterface,
                                                  deltaAnchorPosition, lastPosition);
    mCamera->clampToScrollRange(firstPosition, lastPosition);
}

void GridLayer::updateRowsForLayout(int forState) {
    // Fullscreen is one photo at a time, so its single row is not a choice.
    if (forState == GridLayer::STATE_FULL_SCREEN || mLayoutInterface == nullptr) {
        return;
    }
    GridLayoutInterface *layout = (GridLayoutInterface *)mLayoutInterface;
    layout->mNumRows = rowsForViewport(layout->mSpacingX, layout->mSpacingY);
}

GridLayer::GridLayer(int itemWidth, int itemHeight, LayoutInterface *layoutInterface, RenderView *view)
    : mLayoutInterface(layoutInterface), mBackground(this), mLoading(this), mView(view) {
    mBufferedVisibleRange.set(Shared::INVALID, Shared::INVALID);
    mVisibleRange.set(Shared::INVALID, Shared::INVALID);
    mCompleteRange.set(Shared::INVALID, Shared::INVALID);
    mPreviousDataRange.set(Shared::INVALID, Shared::INVALID);
    mDeltaAnchorPosition.set(0.0f, 0.0f, 0.0f);
    mDeltaAnchorPositionUncommited.set(0.0f, 0.0f, 0.0f);
    mSelectedBucketList.clear();

    mCamera = std::make_unique<GridCamera>(0, 0, itemWidth, itemHeight);
    mDrawables = std::make_unique<GridDrawables>(itemWidth, itemHeight);

    mHud.setGridLayer(this);
    mHud.getPathBar()->clear();
    mHud.getTimeBar()->setListener(this);
    mHud.getPathBar()->pushLabel(Res::drawable::icon_home_small, Res::string::app_name, [this]() {
        if (mHud.getAlpha() == 1.0f) {
            // Already home, the crumb offers the other places photos can come
            // from, where there are any.
            if (mState == STATE_MEDIA_SETS && mHud.showSourceMenu()) {
                return;
            }
            if (!mFeedAboutToChange) {
                setState(STATE_MEDIA_SETS);
            }
        } else {
            mHud.setAlpha(1.0f);
        }
    });

    mCameraManager = std::make_unique<GridCameraManager>(mCamera.get());
    mDrawManager = std::make_unique<GridDrawManager>(mCamera.get(), mDrawables.get(), &mDisplayList, mDisplayItems,
                                                     mDisplaySlots);
    mInputProcessor = std::make_unique<GridInputProcessor>(mCamera.get(), this, mView, mDisplayItems);
    setState(STATE_MEDIA_SETS);
}

GridLayer::~GridLayer() {
    shutdown();
}

void GridLayer::shutdown() {
    if (mMediaFeed) {
        mMediaFeed->shutdown();
    }
    // While the view is still alive to take the GL names back.
    GridDrawables::releaseStringTextures();
    mSelectedBucketList.clear();
    mView = nullptr;
}

void GridLayer::stop() {
    endSlideshow();
    mBackground.clear();
    handleLowMemory();
}

void GridLayer::generate(RenderView *view, RenderLists &lists) {
    lists.updateList.push_back(this);
    lists.opaqueList.push_back(this);
    mBackground.generate(view, lists);
    lists.blendedList.push_back(this);
    lists.hitTestList.push_back(this);
    mHud.generate(view, lists);
    // Last, so the sheet covers the wall and the chrome while it is up.
    mLoading.generate(view, lists);
}

void GridLayer::onSizeChanged() {
    mHud.setSize(mWidth, mHeight);
    mHud.setAlpha(1.0f);
    mBackground.setSize(mWidth, mHeight);
    if (mView) {
        mView->requestRender();
    }
}

void GridLayer::setState(int state) {
    bool feedUnchanged = (mState == state);
    mCamera->mFriction = 0.0f;
    GridLayoutInterface *layoutInterface = (GridLayoutInterface *)mLayoutInterface;
    GridLayoutInterface *oldLayout = &sFullScreenLayoutInterface;
    oldLayout->mNumRows = layoutInterface->mNumRows;
    oldLayout->mSpacingX = layoutInterface->mSpacingX;
    oldLayout->mSpacingY = layoutInterface->mSpacingY;

    GridCamera *camera = mCamera.get();
    (void)camera;
    MediaFeed *feed = mMediaFeed.get();
    bool performLayout = true;
    mZoomValue = 1.0f;
    float yStretch = camera->mDefaultAspectRatio / camera->mAspectRatio;
    if (yStretch < 1.0f) {
        yStretch = 1.0f;
    }

    switch (state) {
    case STATE_GRID_VIEW:
        mTimeElapsedSinceGridViewReady = 0.0f;
        if (feed != nullptr && !feedUnchanged) {
            if (feed->restorePreviousClusteringState()) {
                performLayout = false;
            }
        }
        layoutInterface->mSpacingX = (int)(10 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(10 * App::PIXEL_DENSITY);
        updateRowsForLayout(state);
        if (mState == STATE_MEDIA_SETS) {
            // Entering an album.
            mInAlbum = true;
            MediaSet *set = feed ? feed->getCurrentSet() : nullptr;
            if (set != nullptr) {
                mHud.getPathBar()->pushLabel(mDrawables->getIconForSet(set, true), set->mNoCountTitleString,
                                             [this]() {
                                                 if (mFeedAboutToChange) {
                                                     return;
                                                 }
                                                 if (mHud.getAlpha() == 1.0f) {
                                                     disableLocationFiltering();
                                                     mInputProcessor->clearSelection();
                                                     setState(STATE_GRID_VIEW);
                                                 } else {
                                                     mHud.setAlpha(1.0f);
                                                 }
                                             });
            }
        }
        if (mState == STATE_FULL_SCREEN) {
            mHud.getPathBar()->popLabel();
        }
        break;
    case STATE_TIMELINE:
        mTimeElapsedSinceStackViewReady = 0.0f;
        if (feed != nullptr && !feedUnchanged) {
            feed->performClustering();
            performLayout = false;
        }
        disableLocationFiltering();
        layoutInterface->mSpacingX = (int)(100 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(kStackSpacingY * App::PIXEL_DENSITY * yStretch);
        updateRowsForLayout(state);
        break;
    case STATE_FULL_SCREEN:
        layoutInterface->mNumRows = 1;
        layoutInterface->mSpacingX = (int)(40 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(40 * App::PIXEL_DENSITY);
        if (mState != STATE_FULL_SCREEN) {
            // Photo breadcrumb: fullscreenSelectionChanged supplies position; tapping toggles
            // caption.
            mHud.getPathBar()->pushLabel(Res::drawable::ic_fs_details, "", [this]() {
                if (mHud.getAlpha() == 1.0f) {
                    mHud.swapFullscreenLabel();
                }
                mHud.setAlpha(1.0f);
            });
        }
        break;
    case STATE_MEDIA_SETS:
        mTimeElapsedSinceStackViewReady = 0.0f;
        if (feed != nullptr && !feedUnchanged) {
            feed->restorePreviousClusteringState();
            mMarkedBucketList.clear();
            feed->expandMediaSet(Shared::INVALID);
            performLayout = false;
        }
        disableLocationFiltering();
        mInputProcessor->clearSelection();
        layoutInterface->mSpacingX = (int)(100 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(kStackSpacingY * App::PIXEL_DENSITY * yStretch);
        updateRowsForLayout(state);
        if (mInAlbum) {
            if (mState == STATE_FULL_SCREEN) {
                mHud.getPathBar()->popLabel();
            }
            mHud.getPathBar()->popLabel();
            mInAlbum = false;
        }
        break;
    default:
        break;
    }
    mState = state;
    mHud.onGridStateChanged();
    if (performLayout && !mFeedAboutToChange) {
        onLayout(Shared::INVALID, Shared::INVALID, oldLayout);
    }
    if (state != STATE_FULL_SCREEN) {
        mCamera->moveYTo(0.0f);
        mCamera->moveZTo(0.0f);
    }
}

void GridLayer::enableLocationFiltering(const std::string &label) {
    if (!mLocationFilter) {
        mLocationFilter = true;
        mHud.getPathBar()->pushLabel(Res::drawable::icon_location_small, label, [this]() {
            if (mHud.getAlpha() != 1.0f) {
                mHud.setAlpha(1.0f);
                return;
            }
            if (mState == STATE_FULL_SCREEN) {
                mInputProcessor->clearSelection();
                setState(STATE_GRID_VIEW);
            } else {
                disableLocationFiltering();
            }
        });
    }
}

void GridLayer::disableLocationFiltering() {
    if (mLocationFilter) {
        mLocationFilter = false;
        if (mMediaFeed) {
            mMediaFeed->removeFilter();
        }
        mHud.getPathBar()->popLabel();
    }
}

bool GridLayer::goBack() {
    if (mFeedAboutToChange) {
        return false;
    }
    int state = mState;
    if (mInputProcessor->getCurrentSelectedSlot() == Shared::INVALID && mLocationFilter) {
        disableLocationFiltering();
        setState(STATE_TIMELINE);
        return true;
    }
    switch (state) {
    case STATE_GRID_VIEW:
        setState(STATE_MEDIA_SETS);
        break;
    case STATE_TIMELINE:
        setState(STATE_GRID_VIEW);
        break;
    case STATE_FULL_SCREEN:
        setState(STATE_GRID_VIEW);
        mInputProcessor->clearSelection();
        break;
    default:
        return false;
    }
    return true;
}

void GridLayer::endSlideshow() {
    if (mSlideshowMode) {
        // Pair SDL's counted screensaver enable with startSlideshow's disable.
        SDL_EnableScreenSaver();
    }
    mSlideshowMode = false;
    mHud.setAlpha(1.0f);
}

DataSource *GridLayer::getDataSource() {
    return mMediaFeed ? mMediaFeed->getDataSource() : nullptr;
}

void GridLayer::setDataSource(DataSource *dataSource) {
    std::unique_ptr<MediaFeed> feed = std::move(mMediaFeed);
    mMediaFeed = std::make_unique<MediaFeed>(dataSource, this);
    if (feed) {
        mMediaFeed->copySlotStateFrom(*feed);
        feed->shutdown();
        // A queued or running load holds a bare pointer to one of the old
        // feed's items, and through it to the old source.
        mView->cancelLoads();
        feed.reset();
        clearDisplayList();
        mBackground.clear();
    }
    mMediaFeed->start();
}

void GridLayer::clearDisplayList() {
    // Clear slot pointers when their owning display list is emptied.
    mDisplayList.clear();
    for (int i = 0; i < MAX_ITEMS_DRAWABLE; ++i) {
        mDisplayItems[i] = nullptr;
    }
}

int GridLayer::hitTest(const Vector3f &worldPos, int itemWidth, int itemHeight) {
    int retVal = Shared::INVALID;
    int firstSlotIndex = mVisibleRange.begin;
    int lastSlotIndex = mVisibleRange.end;
    float itemWidthBy2 = (float)itemWidth * 0.5f;
    float itemHeightBy2 = (float)itemHeight * 0.5f;
    Vector3f position;
    Vector3f deltaAnchorPosition(mDeltaAnchorPosition);
    for (int i = firstSlotIndex; i <= lastSlotIndex; ++i) {
        GridCameraManager::getSlotPositionForSlotIndex(i, mCamera.get(), mLayoutInterface, deltaAnchorPosition,
                                                       position);
        if (FloatUtils::boundsContainsPoint(position.x - itemWidthBy2, position.x + itemWidthBy2,
                                            position.y - itemHeightBy2, position.y + itemHeightBy2, worldPos.x,
                                            worldPos.y)) {
            retVal = i;
            break;
        }
    }
    return retVal;
}

void GridLayer::centerCameraForSlot(int slotIndex, float baseConvergence) {
    float imageTheta = 0.0f;
    DisplayItem *displayItem = getDisplayItemForSlotId(slotIndex);
    if (displayItem != nullptr) {
        imageTheta = displayItem->getImageTheta();
    }
    mCameraManager->centerCameraForSlot(mLayoutInterface, slotIndex, baseConvergence, mDeltaAnchorPositionUncommited,
                                        mInputProcessor->getCurrentSelectedSlot(), mZoomValue, imageTheta, mState);
}

bool GridLayer::constrainCameraForSlot(int slotIndex) {
    return mCameraManager->constrainCameraForSlot(mLayoutInterface, slotIndex, mDeltaAnchorPosition,
                                                  mCurrentFocusItemWidth, mCurrentFocusItemHeight);
}

bool GridLayer::update(RenderView *view, float timeElapsed) {
    if (mMediaFeed) {
        mMediaFeed->pumpListener();
    }
    if (!mFeedAboutToChange) {
        mTimeElapsedSinceGridViewReady = std::min(1.0f, mTimeElapsedSinceGridViewReady + timeElapsed);
        mTimeElapsedSinceStackViewReady = std::min(1.0f, mTimeElapsedSinceStackViewReady + timeElapsed);
    }
    if (mRequestToEnterSelection) {
        mHud.enterSelectionMode();
        if (mHud.getMode() == HudLayer::MODE_SELECT) {
            mRequestToEnterSelection = false;
            addSlotToSelectedItems(mInputProcessor->getCurrentSelectedSlot(), true, true);
        }
    }
    if (mMediaFeed && mMediaFeed->isSingleImageMode()) {
        mHud.getPathBar()->setHidden(true);
        mHud.getMenuBar()->setHidden(true);
        if (mHud.getMode() != HudLayer::MODE_NORMAL) {
            mHud.setMode(HudLayer::MODE_NORMAL);
        }
    }

    GridCamera *camera = mCamera.get();
    camera->update(timeElapsed);
    DisplayItem *anchorDisplayItem = getAnchorDisplayItem(ANCHOR_CENTER);
    if (anchorDisplayItem != nullptr && !mHud.getTimeBar()->isDragged()) {
        mHud.getTimeBar()->setItem(anchorDisplayItem->mItemRef);
    }
    mDisplayList.update(timeElapsed);
    mInputProcessor->update(timeElapsed);
    keepWallInRange();
    mSelectedAlpha = FloatUtils::animate(mSelectedAlpha, mTargetAlpha, timeElapsed * 0.5f);
    if (mState == STATE_FULL_SCREEN) {
        mHud.autoHide(true);
    } else {
        mHud.autoHide(false);
        mHud.setAlpha(1.0f);
    }
    for (int i = 0; i < 3; ++i) {
        GridDrawables::sFullscreenGrid[i]->update(timeElapsed);
    }
    if (mSlideshowMode && mState == STATE_FULL_SCREEN) {
        mTimeElapsedSinceView += timeElapsed;
        if (mTimeElapsedSinceView > SLIDESHOW_TRANSITION_TIME) {
            mTimeElapsedSinceView = 0.0f;
            changeFocusToNextSlot(0.5f);
            mCamera->commitMoveInX();
            mCamera->commitMoveInY();
        }
    }
    if (mState == STATE_MEDIA_SETS || mState == STATE_TIMELINE) {
        mCamera->moveYTo(-0.1f);
        mCamera->commitMoveInY();
    }
    bool dirty = mDrawManager->update(timeElapsed);
    dirty |= mSlideshowMode;
    dirty |= mDirtyFrames > 0 || mDirtySeconds > 0.0f;
    ++mFrameCount;
    if (mDirtyFrames > 0) {
        --mDirtyFrames;
    }
    if (mDirtySeconds > 0.0f) {
        mDirtySeconds -= timeElapsed;
    }
    (void)view;
    return mDisplayList.getNumAnimatables() != 0 || mCamera->isAnimating() || mSelectedAlpha != mTargetAlpha || dirty;
}

void GridLayer::computeVisibleRange() {
    if (mPerformingLayoutChange) {
        return;
    }
    if (!mDeltaAnchorPosition.equals(mDeltaAnchorPositionUncommited)) {
        mDeltaAnchorPosition.set(mDeltaAnchorPositionUncommited);
    }
    mCameraManager->computeVisibleRange(mMediaFeed.get(), mLayoutInterface, mDeltaAnchorPosition, mVisibleRange,
                                        mBufferedVisibleRange, mCompleteRange, mState);

    // Clamp the buffered range to the fixed display arrays, trimming the far end first.
    const int span = mBufferedVisibleRange.end - mBufferedVisibleRange.begin + 1;
    if (span > MAX_DISPLAY_SLOTS) {
        mBufferedVisibleRange.end = mBufferedVisibleRange.begin + MAX_DISPLAY_SLOTS - 1;
        if (mVisibleRange.end > mBufferedVisibleRange.end) {
            mVisibleRange.end = mBufferedVisibleRange.end;
        }
    }
}

void GridLayer::clearDisplayItems(int begin, int end) {
    if (begin < 0) {
        begin = 0;
    }
    if (end > MAX_ITEMS_DRAWABLE) {
        end = MAX_ITEMS_DRAWABLE;
    }
    for (int i = begin; i < end; ++i) {
        mDisplayItems[i] = nullptr;
    }
}

void GridLayer::computeVisibleItems() {
    if (mFeedAboutToChange || mPerformingLayoutChange) {
        return;
    }
    computeVisibleRange();
    requestMoreItemsIfNearTheEnd();
    int deltaBegin = mBufferedVisibleRange.begin - mPreviousDataRange.begin;
    int deltaEnd = mBufferedVisibleRange.end - mPreviousDataRange.end;
    if (deltaBegin == 0 && deltaEnd == 0) {
        return;
    }

    // The range moved, so the display items have to be laid out again.
    int firstVisibleSlotIndex = mBufferedVisibleRange.begin;
    int lastVisibleSlotIndex = mBufferedVisibleRange.end;
    mPreviousDataRange.begin = firstVisibleSlotIndex;
    mPreviousDataRange.end = lastVisibleSlotIndex;

    Vector3f position;
    Vector3f deltaAnchorPosition(mDeltaAnchorPosition);
    MediaFeed *feed = mMediaFeed.get();
    LayoutInterface *layout = mLayoutInterface;
    GridCamera *camera = mCamera.get();
    std::vector<MediaItem *> bestItems;

    for (int i = firstVisibleSlotIndex; i <= lastVisibleSlotIndex; ++i) {
        GridCameraManager::getSlotPositionForSlotIndex(i, camera, layout, deltaAnchorPosition, position);
        MediaSet *set = feed ? feed->getSetForSlot(i) : nullptr;
        int indexIntoSlots = i - firstVisibleSlotIndex;
        if (indexIntoSlots < 0 || indexIntoSlots >= MAX_DISPLAY_SLOTS) {
            continue;
        }
        const int baseIndex = indexIntoSlots * MAX_ITEMS_PER_SLOT;
        if (set == nullptr) {
            // Clear unloaded slots so they cannot retain pointers freed by the display list.
            clearDisplayItems(baseIndex, baseIndex + MAX_ITEMS_PER_SLOT);
            continue;
        }

        const std::vector<MediaItem *> &items = set->getItems();
        mDisplaySlots[indexIntoSlots].setMediaSet(set);

        bestItems.clear();
        // Keep showing the same top thumbnails for a stack while scrolling.
        computeSortedIntersection(mVisibleItems, items, MAX_ITEMS_PER_SLOT, bestItems);

        int numItemsInSet = set->getNumItems();
        int originallyFoundItems = (int)bestItems.size();
        if ((int)bestItems.size() < MAX_ITEMS_PER_SLOT) {
            int itemsRemaining = MAX_ITEMS_PER_SLOT - (int)bestItems.size();
            for (int currItemPos = 0; currItemPos < numItemsInSet; ++currItemPos) {
                MediaItem *item = items[(size_t)currItemPos];
                if (!contains(bestItems, item)) {
                    bestItems.push_back(item);
                    if (--itemsRemaining == 0) {
                        break;
                    }
                }
            }
        }
        int numBestItems = (int)bestItems.size();
        for (int j = 0; j < numBestItems; ++j) {
            if (baseIndex + j >= MAX_ITEMS_DRAWABLE) {
                break;
            }
            if (j >= numItemsInSet) {
                mDisplayItems[baseIndex + j] = nullptr;
                continue;
            }
            MediaItem *item = bestItems[(size_t)j];
            if (item == nullptr) {
                continue;
            }
            DisplayItem *displayItem = mDisplayList.get(item);
            displayItem->takeLateDetails();
            if ((mState == STATE_FULL_SCREEN && i != mInputProcessor->getCurrentSelectedSlot()) ||
                (mState == STATE_GRID_VIEW && j >= originallyFoundItems)) {
                displayItem->set(position, j, false);
                displayItem->commit();
            } else {
                mDisplayList.setPositionAndStackIndex(displayItem, position, j, true);
            }
            mDisplayItems[baseIndex + j] = displayItem;
        }
        clearDisplayItems(baseIndex + numBestItems, baseIndex + MAX_ITEMS_PER_SLOT);
    }

    // Clear entries beyond a shortened range.
    clearDisplayItems((lastVisibleSlotIndex - firstVisibleSlotIndex + 1) * MAX_ITEMS_PER_SLOT, MAX_ITEMS_DRAWABLE);

    if (mFeedChanged) {
        mFeedChanged = false;
        if (mState == STATE_FULL_SCREEN && mRequestFocusContentUri.empty()) {
            int currentSelectedSlot = mInputProcessor->getCurrentSelectedSlot();
            if (currentSelectedSlot > mCompleteRange.end) {
                currentSelectedSlot = mCompleteRange.end;
            }
            mInputProcessor->setCurrentSelectedSlot(currentSelectedSlot);
        }
        if (mState == STATE_GRID_VIEW && feed != nullptr) {
            MediaSet *expandedSet = feed->getExpandedMediaSet();
            if (expandedSet != nullptr) {
                PathBarLayer *pathBar = mHud.getPathBar();
                if (pathBar->getCurrentLabel() != expandedSet->mNoCountTitleString) {
                    pathBar->changeLabel(expandedSet->mNoCountTitleString);
                }
            }
        }
        if (!mRequestFocusContentUri.empty() && feed != nullptr) {
            int numSlots = mCompleteRange.end + 1;
            for (int i = 0; i < numSlots; ++i) {
                MediaSet *set = feed->getSetForSlot(i);
                if (set == nullptr) {
                    continue;
                }
                for (MediaItem *item : set->getItems()) {
                    if (item != nullptr && item->mContentUri == mRequestFocusContentUri) {
                        if (mState == STATE_FULL_SCREEN) {
                            mInputProcessor->setCurrentSelectedSlot(i);
                        } else {
                            centerCameraForSlot(i, 1.0f);
                        }
                        break;
                    }
                }
            }
            mRequestFocusContentUri.clear();
        }
    }

    // Keep a bounded number of thumbnails alive.
    int numThumbnailsToKeepInMemory = (mState == STATE_MEDIA_SETS || mState == STATE_TIMELINE) ? 100 : 400;
    int startMemoryRange = (mBufferedVisibleRange.begin / numThumbnailsToKeepInMemory) * numThumbnailsToKeepInMemory;
    if (mStartMemoryRange != startMemoryRange) {
        mStartMemoryRange = startMemoryRange;
        clearUnusedThumbnails();
    }
}

void GridLayer::handleLowMemory() {
    clearUnusedThumbnails();
    GridDrawables::sStringTextureTable.clear();
    mBackground.clearCache();
}

void GridLayer::clearUnusedThumbnails() {
    mDisplayList.clearExcept(mDisplayItems, MAX_ITEMS_DRAWABLE);
}

void GridLayer::onSurfaceCreated(RenderView *view) {
    clearDisplayList();
    mHud.clear();
    mHud.reset();
    GridDrawables::sStringTextureTable.clear();
    mDrawables->onSurfaceCreated(view);
    mBackground.clear();
}

void GridLayer::requestMoreItemsIfNearTheEnd() {
    // Fetch the next page within one screenful of the loaded items' end.
    if (mState != STATE_GRID_VIEW || !mMediaFeed) {
        return;
    }
    MediaSet *expanded = mMediaFeed->getExpandedMediaSet();
    if (expanded == nullptr) {
        return;
    }
    // Nothing to fetch if everything the source has is already here.
    if (expanded->getNumItems() >= expanded->getNumExpectedItems()) {
        return;
    }
    // Allow only one pending page request.
    if (mMediaFeed->isLoadingItemsForSet(expanded)) {
        return;
    }

    const int loaded = expanded->getNumItems();
    // A screen's worth of slack, so the next page is on its way before the
    // empty slots would come into view.
    const int lookAhead = MAX_ITEMS_DRAWABLE;
    if (mBufferedVisibleRange.end + lookAhead < loaded) {
        return;
    }
    mMediaFeed->loadItemsForSet(expanded);
}

void GridLayer::onPointerMoved(float x, float y) {
    mHud.onPointerMoved(x, y);
    mPointerX = x;
    mPointerY = y;
}

void GridLayer::onAccelerometer(float x, float y, float z) {
    if (mInputProcessor) {
        mInputProcessor->onSensorChanged(mView, x, y, z, mState);
    }
}

void GridLayer::onDensityChanged() {
    if (mView == nullptr) {
        return;
    }
    const int itemWidth = itemWidthForDensity();
    const int itemHeight = itemHeightForDensity();

    mCamera->mItemWidth = itemWidth;
    mCamera->mItemHeight = itemHeight;
    ((GridLayoutInterface *)mLayoutInterface)->onDensityChanged();

    // Rebuild shared quads for the new cell size and density.
    GridDrawables::releaseQuads();
    GridDrawables::buildQuads(itemWidth, itemHeight);

    // Drop drawable caches so onSurfaceCreated loads the new density bucket.
    mView->clearCache();
    onSurfaceCreated(mView);

    // Invalidate density-dependent thumbnails; reload as the wall scrolls.
    clearUnusedThumbnails();

    // Relayout chrome even if window dimensions are unchanged: density affects all positions.
    mHud.relayout();
    mBackground.relayout();
}

void GridLayer::onSurfaceChanged(RenderView *view, int width, int height) {
    mCamera->viewportChanged(width, height, (float)mCamera->mItemWidth, (float)mCamera->mItemHeight);
    view->setFov(mCamera->mFov);
    setState(mState);
}

void GridLayer::renderOpaque(RenderView *view) {
    GridCamera *camera = mCamera.get();
    int selectedSlotIndex = mInputProcessor->getCurrentSelectedSlot();
    computeVisibleItems();

    view->glLoadIdentity();
    // The eye coordinates are negated, so the wall is seen from behind. The
    // texture coordinates in GridQuad are mirrored to match.
    view->gluLookAt(-camera->mEyeX, -camera->mEyeY, -camera->mEyeZ, -camera->mLookAtX, -camera->mLookAtY,
                    -camera->mLookAtZ, camera->mUpX, camera->mUpY, camera->mUpZ);
    view->setAlpha(1.0f);
    if (mSelectedAlpha != 1.0f) {
        view->enableBlend(true);
        view->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        view->setAlpha(mSelectedAlpha);
    }
    mTargetAlpha = (selectedSlotIndex != Shared::INVALID) ? 0.0f : 1.0f;

    // The stack or photo under a mouse that is only passing over. Worked out
    // every frame, since the wall scrolls under a pointer that stays still.
    // Not while a press or drag is under way, over the bar, or in fullscreen.
    int hoverSlot = Shared::INVALID;
    if (mPointerX >= 0.0f && mPointerY >= 0.0f && mState != STATE_FULL_SCREEN &&
        selectedSlotIndex == Shared::INVALID && !mInputProcessor->touchPressed() &&
        !mHud.containsPoint(mPointerX, mPointerY)) {
        hoverSlot = getSlotIndexForScreenPosition((int)mPointerX, (int)mPointerY);
    }
    mDrawManager->prepareDraw(mBufferedVisibleRange, mVisibleRange, selectedSlotIndex,
                              mInputProcessor->getCurrentFocusSlot(), mInputProcessor->getCurrentScaledSlot(),
                              mInputProcessor->isFocusItemPressed(), hoverSlot, mInputProcessor->getScale(),
                              mInputProcessor->getScaleGestureDetector(), mFeedAboutToChange);
    if (mSelectedAlpha != 0.0f) {
        mDrawManager->drawThumbnails(view, mState);
    }
    if (mSelectedAlpha != 1.0f) {
        view->enableBlend(false);
    }
    if (selectedSlotIndex != Shared::INVALID) {
        mDrawManager->drawFocusItems(view, mZoomValue, mSlideshowMode, mTimeElapsedSinceView);
        mCurrentFocusItemWidth = mDrawManager->getFocusQuadWidth();
        mCurrentFocusItemHeight = mDrawManager->getFocusQuadHeight();
    }
    view->setAlpha(mSelectedAlpha);
    // Restore the blend mode the render view set up for the blended pass.
    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void GridLayer::renderBlended(RenderView *view) {
    if (mMediaFeed) {
        mDrawManager->drawBlendedComponents(
            view, mSelectedAlpha, mState, mHud.getMode(), mTimeElapsedSinceStackViewReady,
            mTimeElapsedSinceGridViewReady, mSelectedBucketList, mMarkedBucketList,
            mMediaFeed->getWaitingForMediaScanner() || mFeedAboutToChange || mMediaFeed->isLoading());
    }
}

void GridLayer::onLayout(int newAnchorSlotIndex, int currentAnchorSlotIndex, LayoutInterface *oldLayout) {
    if (mPerformingLayoutChange || !mDeltaAnchorPosition.equals(mDeltaAnchorPositionUncommited)) {
        return;
    }
    mPerformingLayoutChange = true;
    LayoutInterface *layout = mLayoutInterface;
    if (oldLayout == nullptr) {
        oldLayout = &sFullScreenLayoutInterface;
    }
    GridCamera *camera = mCamera.get();
    if (currentAnchorSlotIndex == Shared::INVALID) {
        currentAnchorSlotIndex = getAnchorSlotIndex(ANCHOR_CENTER);
        if (mCurrentExpandedSlot != Shared::INVALID) {
            currentAnchorSlotIndex = mCurrentExpandedSlot;
        }
        int selectedSlotIndex = mInputProcessor->getCurrentSelectedSlot();
        if (selectedSlotIndex != Shared::INVALID) {
            currentAnchorSlotIndex = selectedSlotIndex;
        }
    }
    if (newAnchorSlotIndex == Shared::INVALID) {
        newAnchorSlotIndex = currentAnchorSlotIndex;
    }
    int itemHeight = camera->mItemHeight;
    int itemWidth = camera->mItemWidth;

    Vector3f deltaAnchorPosition;
    Vector3f currentSlotPosition;
    if (currentAnchorSlotIndex != Shared::INVALID && newAnchorSlotIndex != Shared::INVALID) {
        layout->getPositionForSlotIndex(newAnchorSlotIndex, itemWidth, itemHeight, deltaAnchorPosition);
        oldLayout->getPositionForSlotIndex(currentAnchorSlotIndex, itemWidth, itemHeight, currentSlotPosition);
        currentSlotPosition.subtract(mDeltaAnchorPosition);
        deltaAnchorPosition.subtract(currentSlotPosition);
        deltaAnchorPosition.y = 0.0f;
        deltaAnchorPosition.z = 0.0f;
    }
    mDeltaAnchorPositionUncommited.set(deltaAnchorPosition);

    centerCameraForSlot(newAnchorSlotIndex, 1.0f);
    mCurrentExpandedSlot = Shared::INVALID;

    // Force the visible items and their positions to be recomputed.
    ((GridLayoutInterface *)oldLayout)->mNumRows = ((GridLayoutInterface *)layout)->mNumRows;
    ((GridLayoutInterface *)oldLayout)->mSpacingX = ((GridLayoutInterface *)layout)->mSpacingX;
    ((GridLayoutInterface *)oldLayout)->mSpacingY = ((GridLayoutInterface *)layout)->mSpacingY;
    forceRecomputeVisibleRange();
    mPerformingLayoutChange = false;
}

void GridLayer::forceRecomputeVisibleRange() {
    mPreviousDataRange.begin = Shared::INVALID;
    mPreviousDataRange.end = Shared::INVALID;
    if (mView) {
        mView->requestRender();
    }
}

void GridLayer::onFeedAboutToChange(MediaFeed *feed) {
    (void)feed;
    mFeedAboutToChange = true;
}

void GridLayer::onFeedChanged(MediaFeed *feed, bool needsLayout) {
    if (!needsLayout && !mFeedAboutToChange) {
        // Sets appended at the end: the rows follow the new slot count, but the
        // camera stays where the user has it.
        updateRowsForLayout(mState);
        mFeedChanged = true;
        forceRecomputeVisibleRange();
        if (mState == STATE_GRID_VIEW || mState == STATE_FULL_SCREEN) {
            mHud.setFeed(feed, mState, needsLayout);
        }
        return;
    }

    if (mState == STATE_GRID_VIEW) {
        MediaSet *set = feed->getCurrentSet();
        if (set != nullptr && !mLocationFilter) {
            mHud.getPathBar()->changeLabel(set->mNoCountTitleString);
        }
    }

    // Recompute row count when the feed's slot count changes. Outside setState,
    // so the current state is the one to lay out for.
    updateRowsForLayout(mState);

    int firstBufferedVisibleSlotIndex = mBufferedVisibleRange.begin;
    int lastBufferedVisibleSlotIndex = mBufferedVisibleRange.end;
    int currentlyVisibleSlotIndex = getAnchorSlotIndex(ANCHOR_CENTER);
    int numVisibleItems = mVisibleRange.end - mVisibleRange.begin + 1;
    if (mState == STATE_MEDIA_SETS && currentlyVisibleSlotIndex < numVisibleItems) {
        currentlyVisibleSlotIndex = getAnchorSlotIndex(ANCHOR_LEFT);
    }
    if (mCurrentExpandedSlot != Shared::INVALID) {
        currentlyVisibleSlotIndex = mCurrentExpandedSlot;
    }

    MediaItem *anchorItem = nullptr;
    mVisibleItems.clear();
    if (currentlyVisibleSlotIndex != Shared::INVALID && currentlyVisibleSlotIndex >= firstBufferedVisibleSlotIndex &&
        currentlyVisibleSlotIndex <= lastBufferedVisibleSlotIndex) {
        int baseIndex = (currentlyVisibleSlotIndex - firstBufferedVisibleSlotIndex) * MAX_ITEMS_PER_SLOT;
        for (int i = 0; i < MAX_ITEMS_PER_SLOT; ++i) {
            if (baseIndex + i >= MAX_ITEMS_DRAWABLE) {
                break;
            }
            DisplayItem *displayItem = mDisplayItems[baseIndex + i];
            if (displayItem != nullptr) {
                if (anchorItem == nullptr) {
                    anchorItem = displayItem->mItemRef;
                }
                mVisibleItems.push_back(displayItem->mItemRef);
            }
        }
    }
    // Collect the rest starting from the middle and working outward.
    int numItems = lastBufferedVisibleSlotIndex - firstBufferedVisibleSlotIndex + 1;
    int midPoint = currentlyVisibleSlotIndex;
    for (int i = 0; i < numItems; ++i) {
        int index = midPoint + Shared::midPointIterator(i);
        int indexIntoDisplayItem = (index - firstBufferedVisibleSlotIndex) * MAX_ITEMS_PER_SLOT;
        if (indexIntoDisplayItem < 0 || indexIntoDisplayItem >= MAX_ITEMS_DRAWABLE) {
            continue;
        }
        for (int j = 0; j < MAX_ITEMS_PER_SLOT; ++j) {
            if (indexIntoDisplayItem + j >= MAX_ITEMS_DRAWABLE) {
                break;
            }
            DisplayItem *displayItem = mDisplayItems[indexIntoDisplayItem + j];
            if (displayItem != nullptr && !contains(mVisibleItems, displayItem->mItemRef)) {
                mVisibleItems.push_back(displayItem->mItemRef);
            }
        }
    }

    int newSlotIndex = Shared::INVALID;
    if (anchorItem != nullptr) {
        int numSlots = feed->getNumSlots();
        for (int i = 0; i < numSlots; ++i) {
            MediaSet *set = feed->getSetForSlot(i);
            if (set != nullptr && contains(set->getItems(), anchorItem)) {
                newSlotIndex = i;
                break;
            }
        }
        if (newSlotIndex == Shared::INVALID && anchorItem->mParentMediaSet != nullptr) {
            for (int i = 0; i < numSlots; ++i) {
                MediaSet *set = feed->getSetForSlot(i);
                if (set != nullptr && set->mId == anchorItem->mParentMediaSet->mId) {
                    newSlotIndex = i;
                    break;
                }
            }
        }
    }

    if (newSlotIndex != Shared::INVALID) {
        if (mState == STATE_MEDIA_SETS) {
            mDisplayList.clearExcept(mDisplayItems, MAX_ITEMS_DRAWABLE);
        }
        onLayout(newSlotIndex, currentlyVisibleSlotIndex, nullptr);
    } else {
        forceRecomputeVisibleRange();
    }
    mCurrentExpandedSlot = Shared::INVALID;
    mFeedAboutToChange = false;
    mFeedChanged = true;
    if (mState == STATE_GRID_VIEW || mState == STATE_FULL_SCREEN) {
        mHud.setFeed(feed, mState, needsLayout);
    }
    if (mView) {
        mView->requestRender();
    }
}

int GridLayer::representativeSlotIndex(int state, int focusSlot, int selectedSlot, int anchorCenterSlot) {
    if (focusSlot != Shared::INVALID) {
        return focusSlot;
    }
    if (state == STATE_FULL_SCREEN && selectedSlot != Shared::INVALID) {
        // Fullscreen shows one photo, so the background belongs to that one.
        // The middle of the visible range is a neighbour as often as not, and
        // stepping photo by photo leaves it on the wrong one.
        return selectedSlot;
    }
    return anchorCenterSlot;
}

DisplayItem *GridLayer::getRepresentativeDisplayItem() {
    const int focusSlot = mInputProcessor ? mInputProcessor->getCurrentFocusSlot() : Shared::INVALID;
    const int selectedSlot = mInputProcessor ? mInputProcessor->getCurrentSelectedSlot() : Shared::INVALID;
    const int slotIndex =
        representativeSlotIndex(mState, focusSlot, selectedSlot, getAnchorSlotIndex(ANCHOR_CENTER));
    int index = (slotIndex - mBufferedVisibleRange.begin) * MAX_ITEMS_PER_SLOT;
    if (index >= 0 && index < MAX_ITEMS_DRAWABLE) {
        return mDisplayItems[index];
    }
    return nullptr;
}

DisplayItem *GridLayer::getAnchorDisplayItem(int type) {
    int slotIndex = getAnchorSlotIndex(type);
    int index = (slotIndex - mBufferedVisibleRange.begin) * MAX_ITEMS_PER_SLOT;
    if (index >= 0 && index < MAX_ITEMS_DRAWABLE) {
        return mDisplayItems[index];
    }
    return nullptr;
}

float GridLayer::getScrollPosition() const {
    // In pixels.
    return mCamera->mLookAtX * mCamera->mScale + mDeltaAnchorPosition.x;
}

DisplayItem *GridLayer::getDisplayItemForScrollPosition(float posX) {
    MediaFeed *feed = mMediaFeed.get();
    int itemWidth = mCamera->mItemWidth;
    int itemHeight = mCamera->mItemHeight;
    GridLayoutInterface *gridInterface = (GridLayoutInterface *)mLayoutInterface;
    int left = (int)((posX / (float)itemWidth) * (float)gridInterface->mNumRows);
    int right = feed ? feed->getNumSlots() : 0;
    int retSlot = left;
    Vector3f position;
    for (int i = left; i < right; ++i) {
        gridInterface->getPositionForSlotIndex(i, itemWidth, itemHeight, position);
        retSlot = i;
        if (position.x >= posX) {
            break;
        }
    }
    if (mFeedAboutToChange || right == 0) {
        return nullptr;
    }
    if (retSlot >= right) {
        retSlot = right - 1;
    }
    MediaSet *set = feed->getSetForSlot(retSlot);
    if (set != nullptr && set->getNumItems() > 0) {
        return mDisplayList.get(set->getItems()[0]);
    }
    return nullptr;
}

int GridLayer::getAnchorSlotIndex(int anchorType) const {
    switch (anchorType) {
    case ANCHOR_LEFT:
        return mVisibleRange.begin;
    case ANCHOR_RIGHT:
        return mVisibleRange.end;
    case ANCHOR_CENTER:
        return (mVisibleRange.begin + mVisibleRange.end) / 2;
    default:
        return 0;
    }
}

DisplayItem *GridLayer::getDisplayItemForSlotId(int slotId) {
    int index = slotId - mBufferedVisibleRange.begin;
    if (index >= 0 && slotId <= mBufferedVisibleRange.end && index * MAX_ITEMS_PER_SLOT < MAX_ITEMS_DRAWABLE) {
        return mDisplayItems[index * MAX_ITEMS_PER_SLOT];
    }
    return nullptr;
}

bool GridLayer::changeFocusToNextSlot(float convergence) {
    int currentSelectedSlot = mInputProcessor->getCurrentSelectedSlot();
    bool retVal = changeFocusToSlot(currentSelectedSlot + 1, convergence);
    if (mInputProcessor->getCurrentSelectedSlot() == currentSelectedSlot) {
        endSlideshow();
        mHud.setAlpha(1.0f);
    }
    return retVal;
}

bool GridLayer::changeFocusToSlot(int slotId, float convergence) {
    mZoomValue = 1.0f;
    int index = slotId - mBufferedVisibleRange.begin;
    if (index < 0 || slotId > mBufferedVisibleRange.end || index * MAX_ITEMS_PER_SLOT >= MAX_ITEMS_DRAWABLE) {
        return false;
    }
    DisplayItem *displayItem = mDisplayItems[index * MAX_ITEMS_PER_SLOT];
    if (displayItem == nullptr) {
        return false;
    }
    mHud.fullscreenSelectionChanged(displayItem->mItemRef, slotId + 1, mCompleteRange.end + 1);
    if (slotId != Shared::INVALID && slotId <= mCompleteRange.end) {
        mInputProcessor->setCurrentFocusSlot(slotId);
        centerCameraForSlot(slotId, convergence);
        return true;
    }
    centerCameraForSlot(mInputProcessor->getCurrentSelectedSlot(), convergence);
    return false;
}

bool GridLayer::changeFocusToPreviousSlot(float convergence) {
    return changeFocusToSlot(mInputProcessor->getCurrentSelectedSlot() - 1, convergence);
}

std::vector<MediaBucket> &GridLayer::getSelectedBuckets() {
    return mSelectedBucketList.get();
}

void GridLayer::selectAll() {
    if (mState != STATE_FULL_SCREEN) {
        int numSlots = mCompleteRange.end + 1;
        for (int i = 0; i < numSlots; ++i) {
            addSlotToSelectedItems(i, false, false);
        }
        updateCountOfSelectedItems();
    } else {
        addSlotToSelectedItems(mInputProcessor->getCurrentFocusSlot(), false, true);
    }
}

void GridLayer::deselectOrCancelSelectMode() {
    if (mSelectedBucketList.size() == 0) {
        mHud.cancelSelection();
    } else {
        mSelectedBucketList.clear();
        updateCountOfSelectedItems();
    }
}

void GridLayer::deselectAll() {
    mHud.cancelSelection();
    mSelectedBucketList.clear();
    updateCountOfSelectedItems();
}

void GridLayer::deleteSelection() {
    if (mMediaFeed) {
        mMediaFeed->performOperation(MediaFeed::OPERATION_DELETE, &getSelectedBuckets(), nullptr);
    }
    deselectAll();
    if (mCompleteRange.isEmpty()) {
        goBack();
    }
}

void GridLayer::addSlotToSelectedItems(int slotId, bool removeIfAlreadyAdded, bool updateCount) {
    if (!mFeedAboutToChange && mMediaFeed) {
        mSelectedBucketList.add(slotId, mMediaFeed.get(), removeIfAlreadyAdded);
        if (updateCount) {
            updateCountOfSelectedItems();
            if (mSelectedBucketList.size() == 0) {
                deselectAll();
            }
        }
    }
    mHud.computeBottomMenu();
}

void GridLayer::updateCountOfSelectedItems() {
    mHud.updateNumItemsSelected(mSelectedBucketList.size());
}

int GridLayer::getMetadataSlotIndexForScreenPosition(int posX, int posY) {
    return getSlotForScreenPosition(posX, posY, mCamera->mItemWidth + (int)(100 * App::PIXEL_DENSITY),
                                    mCamera->mItemHeight + (int)(100 * App::PIXEL_DENSITY));
}

int GridLayer::getSlotIndexForScreenPosition(int posX, int posY) {
    return getSlotForScreenPosition(posX, posY, mCamera->mItemWidth, mCamera->mItemHeight);
}

int GridLayer::getSlotForScreenPosition(int posX, int posY, int itemWidth, int itemHeight) {
    Vector3f worldPos;
    GridCamera *camera = mCamera.get();
    camera->convertToCameraSpace((float)posX, (float)posY, 0.0f, worldPos);
    // Slots are expressed in pixels too. Z is ignored.
    worldPos.x *= camera->mScale;
    worldPos.y *= camera->mScale;
    return hitTest(worldPos, itemWidth, itemHeight);
}

bool GridLayer::tapGesture(int slotIndex, bool metadata) {
    MediaFeed *feed = mMediaFeed.get();
    if (feed == nullptr) {
        return false;
    }
    if (!feed->isClustered()) {
        if (!feed->hasExpandedMediaSet()) {
            if (feed->canExpandSet(slotIndex)) {
                mCurrentExpandedSlot = slotIndex;
                feed->expandMediaSet(slotIndex);
                setState(STATE_GRID_VIEW);
            }
            return false;
        }
        return true;
    }
    // Select a cluster and recompute a new cluster inside it.
    mCurrentExpandedSlot = slotIndex;
    mMarkedBucketList.clear();
    mMarkedBucketList.add(slotIndex, feed, false);
    goBack();
    if (metadata) {
        int slotOffset = slotIndex - mBufferedVisibleRange.begin;
        if (slotOffset >= 0 && slotOffset < MAX_DISPLAY_SLOTS) {
            DisplaySlot &slot = mDisplaySlots[slotOffset];
            if (slot.hasValidLocation()) {
                MediaSet *set = slot.getMediaSet();
                if (set != nullptr && !set->mReverseGeocodedLocation.empty()) {
                    enableLocationFiltering(set->mReverseGeocodedLocation);
                }
            }
        }
    }
    return false;
}

void GridLayer::onTimeChanged(TimeBar *timebar) {
    if (mFeedAboutToChange) {
        return;
    }
    MediaItem *item = timebar->getItem();
    MediaFeed *feed = mMediaFeed.get();
    if (item == nullptr || feed == nullptr) {
        return;
    }
    int numSlots = feed->getNumSlots();
    for (int i = 0; i < numSlots; ++i) {
        MediaSet *set = feed->getSetForSlot(i);
        if (set == nullptr || set->getNumItems() == 0) {
            return;
        }
        if (contains(set->getItems(), item)) {
            centerCameraForSlot(i, 1.0f);
            break;
        }
    }
}

void GridLayer::startSlideshow() {
    endSlideshow();
    // Keep the screen awake during slideshows.
    SDL_DisableScreenSaver();
    mSlideshowMode = true;
    mZoomValue = 1.0f;
    centerCameraForSlot(mInputProcessor->getCurrentSelectedSlot(), 1.0f);
    mTimeElapsedSinceView = SLIDESHOW_TRANSITION_TIME - 1.0f;
    mHud.setAlpha(0.0f);
}

void GridLayer::enterSelectionMode() {
    mSlideshowMode = false;
    mHud.enterSelectionMode();
    int currentSlot = mInputProcessor->getCurrentSelectedSlot();
    if (currentSlot == Shared::INVALID) {
        currentSlot = mInputProcessor->getCurrentFocusSlot();
    }
    addSlotToSelectedItems(currentSlot, false, true);
}

bool GridLayer::selectOnlyCurrentItem() {
    mSelectedBucketList.clear();
    const int currentSlot = mInputProcessor->getCurrentSelectedSlot();
    if (currentSlot == Shared::INVALID || mFeedAboutToChange || !mMediaFeed) {
        return false;
    }
    mSelectedBucketList.add(currentSlot, mMediaFeed.get(), false);
    return mSelectedBucketList.size() > 0;
}

void GridLayer::clearSelectedItems() {
    mSelectedBucketList.clear();
}

float GridLayer::getFillScreenZoomValue() {
    return GridCameraManager::getFillScreenZoomValue(mCamera.get(), mCurrentFocusItemWidth, mCurrentFocusItemHeight);
}

void GridLayer::zoomInToSelectedItem() {
    mSlideshowMode = false;
    float potentialZoomValue = getFillScreenZoomValue();
    if (mZoomValue < potentialZoomValue) {
        mZoomValue = potentialZoomValue;
    } else {
        mZoomValue *= 3.0f;
    }
    if (mZoomValue > 6.0f) {
        mZoomValue = 6.0f;
    }
    mHud.setAlpha(1.0f);
    centerCameraForSlot(mInputProcessor->getCurrentSelectedSlot(), 1.0f);
}

void GridLayer::zoomOutFromSelectedItem() {
    mSlideshowMode = false;
    if (mZoomValue == getFillScreenZoomValue()) {
        mZoomValue = 1.0f;
    } else {
        mZoomValue /= 3.0f;
    }
    if (mZoomValue < 1.0f) {
        mZoomValue = 1.0f;
    }
    mHud.setAlpha(1.0f);
    centerCameraForSlot(mInputProcessor->getCurrentSelectedSlot(), 1.0f);
}

void GridLayer::rotateSelectedItems(float f) {
    std::vector<MediaBucket> &mediaBuckets = mSelectedBucketList.get();
    for (MediaBucket &bucket : mediaBuckets) {
        for (MediaItem *item : bucket.mediaItems) {
            DisplayItem *displayItem = mDisplayList.get(item);
            displayItem->rotateImageBy(f);
            mDisplayList.addToAnimatables(displayItem);
        }
    }
    if (mState == STATE_FULL_SCREEN) {
        centerCameraForSlot(mInputProcessor->getCurrentSelectedSlot(), 1.0f);
    }
    if (mMediaFeed) {
        mMediaFeed->performOperation(MediaFeed::OPERATION_ROTATE, &mediaBuckets, &f);
    }
}

bool GridLayer::onTouchEvent(const MotionEvent &event) {
    return mInputProcessor->onTouchEvent(event);
}

bool GridLayer::onKeyDown(int keyCode, const KeyEvent &event) {
    return mInputProcessor->onKeyDown(keyCode, event, mState);
}

bool GridLayer::noDeleteMode() const {
    if (mNoDeleteMode || (mMediaFeed && mMediaFeed->isSingleImageMode())) {
        return true;
    }
    // Offer delete only when the selection's sources support it.
    return !selectionSupports(MediaFeed::OPERATION_DELETE);
}

bool GridLayer::selectionSupports(int operation) const {
    if (mMediaFeed == nullptr) {
        return false;
    }
    return mMediaFeed->selectionSupports(operation, &mSelectedBucketList.get());
}

void GridLayer::setZoomValue(float f) {
    mZoomValue = f;
    // Fast convergence keeps the camera tracking the pinch.
    centerCameraForSlot(mInputProcessor->getCurrentSelectedSlot(), 10.0f);
}
