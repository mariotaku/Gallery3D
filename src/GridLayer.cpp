#include "GridLayer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "App.h"
#include "FloatUtils.h"
#include "LocalDataSource.h"
#include "MediaItem.h"
#include "RenderView.h"
#include "Shared.h"

static const float SLIDESHOW_TRANSITION_TIME = 3.5f;

GridLayoutInterface GridLayer::sFullScreenLayoutInterface(1);

namespace {

// Replaces ArrayUtils.computeSortedIntersection. Identity is the pointer now,
// so the hash filter the original needed is gone.
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

}  // namespace

int GridLayer::itemWidthForDensity() {
    return (int)(96.0f * App::PIXEL_DENSITY);
}

int GridLayer::itemHeightForDensity() {
    return (int)(72.0f * App::PIXEL_DENSITY);
}

GridLayer::GridLayer(int itemWidth, int itemHeight, LayoutInterface *layoutInterface, RenderView *view)
    : mBackground(this), mLoading(this), mView(view), mLayoutInterface(layoutInterface) {
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
    int numMaxRows = (camera->mHeight >= camera->mWidth) ? 4 : 3;
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
        layoutInterface->mNumRows = numMaxRows;
        layoutInterface->mSpacingX = (int)(10 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(10 * App::PIXEL_DENSITY);
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
        layoutInterface->mNumRows = numMaxRows - 1;
        layoutInterface->mSpacingX = (int)(100 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(70 * App::PIXEL_DENSITY * yStretch);
        break;
    case STATE_FULL_SCREEN:
        layoutInterface->mNumRows = 1;
        layoutInterface->mSpacingX = (int)(40 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(40 * App::PIXEL_DENSITY);
        if (mState != STATE_FULL_SCREEN) {
            // A crumb of its own for the photo. It starts blank because
            // fullscreenSelectionChanged fills it in with the position as soon
            // as there is a photo to count, and tapping it swaps between that
            // and the caption.
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
        layoutInterface->mNumRows = numMaxRows - 1;
        layoutInterface->mSpacingX = (int)(100 * App::PIXEL_DENSITY);
        layoutInterface->mSpacingY = (int)(70 * App::PIXEL_DENSITY * yStretch);
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
        // Paired with the disable in startSlideshow. SDL counts these, so the
        // guard matters: ending a slideshow that never started would enable the
        // screensaver on behalf of someone else.
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
        feed.reset();
        clearDisplayList();
        mBackground.clear();
    }
    mMediaFeed->start();
}

void GridLayer::clearDisplayList() {
    // The list owns the items, so emptying it leaves mDisplayItems pointing at
    // freed memory. Anything that reads a slot before the next computeVisibleItems
    // refills it - entering fullscreen, say - reads through those pointers.
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
    dirty |= mFramesDirty > 0;
    ++mFrameCount;
    if (mFramesDirty > 0) {
        --mFramesDirty;
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
}

void GridLayer::computeVisibleItems() {
    if (mFeedAboutToChange || mPerformingLayoutChange) {
        return;
    }
    computeVisibleRange();
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
        if (set == nullptr || indexIntoSlots < 0 || indexIntoSlots >= MAX_DISPLAY_SLOTS) {
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
        int baseIndex = (i - firstVisibleSlotIndex) * MAX_ITEMS_PER_SLOT;
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
            if ((mState == STATE_FULL_SCREEN && i != mInputProcessor->getCurrentSelectedSlot()) ||
                (mState == STATE_GRID_VIEW && j >= originallyFoundItems)) {
                displayItem->set(position, j, false);
                displayItem->commit();
            } else {
                mDisplayList.setPositionAndStackIndex(displayItem, position, j, true);
            }
            mDisplayItems[baseIndex + j] = displayItem;
        }
        for (int j = numBestItems; j < MAX_ITEMS_PER_SLOT; ++j) {
            if (baseIndex + j < MAX_ITEMS_DRAWABLE) {
                mDisplayItems[baseIndex + j] = nullptr;
            }
        }
    }

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

void GridLayer::onPointerMoved(float x, float y) {
    mHud.onPointerMoved(x, y);
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

    // The shared quads are all sized from the cell and the density, so they go
    // back and come out again at the new one.
    GridDrawables::releaseQuads();
    GridDrawables::buildQuads(itemWidth, itemHeight);

    // Drawables are picked from a density bucket, so what is cached is now the
    // art for the wrong screen. Dropping the cache makes onSurfaceCreated fetch
    // them again, and findDrawable answers with the bucket the new density
    // asks for.
    mView->clearCache();
    onSurfaceCreated(mView);

    // Thumbnails are decoded to a size that follows the density too, so the
    // ones in hand are the wrong resolution. They are reloaded as the wall
    // scrolls rather than all at once, which keeps the change cheap.
    clearUnusedThumbnails();

    // The chrome sits in a window that has not changed size, so setSize would
    // decide there was nothing to do. Every position in it is in density units
    // though, so the layout has to run again regardless.
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

    mDrawManager->prepareDraw(mBufferedVisibleRange, mVisibleRange, selectedSlotIndex,
                              mInputProcessor->getCurrentFocusSlot(), mInputProcessor->getCurrentScaledSlot(),
                              mInputProcessor->isFocusItemPressed(), mInputProcessor->getScale(),
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

DisplayItem *GridLayer::getRepresentativeDisplayItem() {
    int slotIndex = mInputProcessor ? mInputProcessor->getCurrentFocusSlot() : Shared::INVALID;
    if (slotIndex == Shared::INVALID) {
        slotIndex = getAnchorSlotIndex(ANCHOR_CENTER);
    }
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
    // The original held a wake lock so the screen would not go out mid
    // slideshow. This is the desktop equivalent.
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
    // And when whatever holds the selection cannot delete. A read only source,
    // a gallery served over an api for instance, should not be offered a button
    // that can only fail.
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
    // Gingerbread raised this from 1.0f so the camera tracks the pinch instead
    // of lagging a frame or two behind it.
    centerCameraForSlot(mInputProcessor->getCurrentSelectedSlot(), 10.0f);
}
