#include "GridInputProcessor.h"

#include <SDL3/SDL.h>

#include <cmath>

#include "FileOperations.h"
#include "FloatUtils.h"
#include "GridCameraManager.h"
#include "GridLayer.h"
#include "HudLayer.h"
#include "MediaFeed.h"
#include "MediaItem.h"
#include "RenderView.h"
#include "Shared.h"

GridInputProcessor::GridInputProcessor(GridCamera *camera, GridLayer *layer, RenderView *view,
                                       DisplayItem **displayItems)
    : mCamera(camera),
      mLayer(layer),
      mView(view),
      mDisplayItems(displayItems),
      mGestureDetector(this),
      mScaleGestureDetector(this) {
    mCurrentFocusSlot = Shared::INVALID;
    mCurrentSelectedSlot = Shared::INVALID;
    mCurrentScaleSlot = Shared::INVALID;
    mGestureDetector.setIsLongpressEnabled(true);
}

void GridInputProcessor::setCurrentSelectedSlot(int slot) {
    mCurrentSelectedSlot = slot;
    GridLayer *layer = mLayer;
    layer->setState(GridLayer::STATE_FULL_SCREEN);
    mCamera->mConvergenceSpeed = 2.0f;
    mCamera->mFriction = 0.0f;
    DisplayItem *displayItem = layer->getDisplayItemForSlotId(slot);
    MediaItem *item = displayItem ? displayItem->mItemRef : nullptr;
    layer->getHud()->fullscreenSelectionChanged(item, mCurrentSelectedSlot + 1, layer->getCompleteRange().end + 1);
}

bool GridInputProcessor::onTouchEvent(const MotionEvent &event) {
    mTouchPosX = (int)event.getX();
    mTouchPosY = (int)event.getY();
    mActionCode = event.getAction();
    uint64_t timestamp = event.eventTime;
    uint64_t delta = (timestamp > mPrevTouchTime) ? (timestamp - mPrevTouchTime) : 0;
    mPrevTouchTime = timestamp;
    float timeElapsed = (float)delta * 0.001f;

    switch (mActionCode) {
    case MotionEvent::ACTION_UP:
        touchEnded(mTouchPosX, mTouchPosY, timeElapsed);
        break;
    case MotionEvent::ACTION_DOWN:
        mPrevTouchTime = timestamp;
        touchBegan(mTouchPosX, mTouchPosY);
        break;
    case MotionEvent::ACTION_MOVE:
        touchMoved(mTouchPosX, mTouchPosY, timeElapsed);
        break;
    default:
        break;
    }
    if (!mZoomGesture) {
        mGestureDetector.onTouchEvent(event);
    }
    mScaleGestureDetector.onTouchEvent(event);
    return true;
}

void GridInputProcessor::onWheel(float focusX, float focusY, float ticks) {
    mScaleGestureDetector.onWheel(focusX, focusY, ticks);
}

bool GridInputProcessor::onKeyDown(int keyCode, const KeyEvent &event, int state) {
    (void)event;
    GridLayer *layer = mLayer;
    if (keyCode == KeyEvent::KEYCODE_BACK) {
        if (layer->getViewIntent()) {
            return false;
        }
        if (layer->getHud()->getMode() == HudLayer::MODE_SELECT) {
            layer->deselectAll();
            return true;
        }
        if (layer->inSlideShowMode()) {
            layer->endSlideshow();
            layer->getHud()->setAlpha(1.0f);
            return true;
        }
        float zoomValue = layer->getZoomValue();
        if (zoomValue != 1.0f) {
            layer->setZoomValue(1.0f);
            layer->centerCameraForSlot(mCurrentSelectedSlot, 1.0f);
            return true;
        }
        layer->goBack();
        return state != GridLayer::STATE_MEDIA_SETS;
    }
    if (mDpadIgnoreTime < 0.1f) {
        return true;
    }
    mDpadIgnoreTime = 0.0f;
    const IndexRange &bufferedVisibleRange = layer->getBufferedVisibleRange();
    int firstBufferedVisibleSlot = bufferedVisibleRange.begin;
    int lastBufferedVisibleSlot = bufferedVisibleRange.end;
    int anchorSlot = layer->getAnchorSlotIndex(GridLayer::ANCHOR_CENTER);

    if (state == GridLayer::STATE_FULL_SCREEN) {
        layer->endSlideshow();
        bool directionalKeyPressed = false;
        if (keyCode == KeyEvent::KEYCODE_DPAD_RIGHT) {
            layer->changeFocusToNextSlot(1.0f);
            directionalKeyPressed = true;
        }
        if (keyCode == KeyEvent::KEYCODE_DPAD_LEFT) {
            layer->changeFocusToPreviousSlot(1.0f);
            directionalKeyPressed = true;
        }
        if (directionalKeyPressed && layer->getHud()->getMode() == HudLayer::MODE_SELECT) {
            layer->deselectAll();
            layer->enterSelectionMode();
        }
        if (keyCode == KeyEvent::KEYCODE_DPAD_CENTER && !mCamera->isAnimating()) {
            if (layer->getZoomValue() == 1.0f) {
                layer->zoomInToSelectedItem();
            } else {
                layer->setZoomValue(1.0f);
            }
        }
        if (keyCode == KeyEvent::KEYCODE_MENU) {
            if (layer->getFeed() != nullptr && layer->getFeed()->isSingleImageMode()) {
                return true;
            }
            if (layer->getHud()->getMode() == HudLayer::MODE_NORMAL) {
                layer->enterSelectionMode();
            } else {
                layer->deselectAll();
            }
        }
        return false;
    }

    mCurrentFocusIsPressed = false;
    int numRows = layer->getLayoutInterface()->mNumRows;
    if (keyCode == KeyEvent::KEYCODE_DPAD_CENTER && mCurrentFocusSlot != Shared::INVALID) {
        if (layer->getHud()->getMode() != HudLayer::MODE_SELECT) {
            bool centerCamera = layer->tapGesture(mCurrentFocusSlot, false);
            if (centerCamera) {
                selectSlot(mCurrentFocusSlot);
            }
            mCurrentFocusSlot = Shared::INVALID;
            return true;
        }
        layer->addSlotToSelectedItems(mCurrentFocusSlot, true, true);
        mCurrentFocusIsPressed = true;
    } else if (keyCode == KeyEvent::KEYCODE_MENU && mCurrentFocusSlot != Shared::INVALID) {
        if (layer->getHud()->getMode() == HudLayer::MODE_NORMAL) {
            layer->enterSelectionMode();
        } else {
            layer->deselectAll();
        }
    } else if (mCurrentFocusSlot == Shared::INVALID) {
        mCurrentFocusSlot = anchorSlot;
    } else if (keyCode == KeyEvent::KEYCODE_DPAD_RIGHT) {
        mCurrentFocusSlot += numRows;
    } else if (keyCode == KeyEvent::KEYCODE_DPAD_LEFT) {
        mCurrentFocusSlot -= numRows;
    } else if (keyCode == KeyEvent::KEYCODE_DPAD_UP) {
        --mCurrentFocusSlot;
    } else if (keyCode == KeyEvent::KEYCODE_DPAD_DOWN) {
        ++mCurrentFocusSlot;
    }
    if (mCurrentFocusSlot > lastBufferedVisibleSlot) {
        mCurrentFocusSlot = lastBufferedVisibleSlot;
    }
    if (mCurrentFocusSlot < firstBufferedVisibleSlot) {
        mCurrentFocusSlot = firstBufferedVisibleSlot;
    }
    if (mCurrentFocusSlot != Shared::INVALID) {
        layer->centerCameraForSlot(mCurrentFocusSlot, 1.0f);
    }
    return false;
}

void GridInputProcessor::touchBegan(int posX, int posY) {
    mPrevTouchPosX = (float)posX;
    mPrevTouchPosY = (float)posY;
    mFirstTouchPosX = (float)posX;
    mFirstTouchPosY = (float)posY;
    mTouchVelX = 0.0f;
    mTouchVelY = 0.0f;
    mProcessTouch = true;
    mZoomGesture = false;
    mTouchMoved = false;
    mCamera->stopMovementInX();
    GridLayer *layer = mLayer;
    mCurrentFocusSlot = layer->getSlotIndexForScreenPosition(posX, posY);
    mCurrentFocusIsPressed = true;
    mTouchFeedbackDelivered = false;
    HudLayer *hud = layer->getHud();
    if (hud->getMode() == HudLayer::MODE_SELECT) {
        hud->closeSelectionMenu();
    }
    if (layer->getState() == GridLayer::STATE_FULL_SCREEN && hud->getMode() == HudLayer::MODE_SELECT) {
        layer->deselectAll();
        hud->setAlpha(1.0f);
    }
}

void GridInputProcessor::touchMoved(int posX, int posY, float timeElapsedx) {
    if (!mProcessTouch || mZoomGesture) {
        return;
    }
    GridLayer *layer = mLayer;
    GridCamera *camera = mCamera;
    // Negated: the wall moves opposite to the finger.
    float deltaX = -((float)posX - mPrevTouchPosX);
    float deltaY = -((float)posY - mPrevTouchPosY);
    if (std::fabs(deltaX) >= 10.0f || std::fabs(deltaY) >= 10.0f) {
        mTouchMoved = true;
    }

    Vector3f firstPosition;
    Vector3f lastPosition;
    Vector3f deltaAnchorPosition;
    Vector3f worldPosDelta;

    deltaAnchorPosition.set(layer->getDeltaAnchorPosition());
    LayoutInterface *layout = layer->getLayoutInterface();
    GridCameraManager::getSlotPositionForSlotIndex(0, camera, layout, deltaAnchorPosition, firstPosition);
    int lastSlotIndex = layer->getCompleteRange().end;
    GridCameraManager::getSlotPositionForSlotIndex(lastSlotIndex, camera, layout, deltaAnchorPosition, lastPosition);

    camera->convertToRelativeCameraSpace(deltaX, deltaY, 0.0f, worldPosDelta);
    deltaX = worldPosDelta.x;
    deltaY = worldPosDelta.y;
    camera->moveBy(deltaX, (layer->getZoomValue() == 1.0f) ? 0.0f : deltaY, 0.0f);
    deltaX *= camera->mScale;
    deltaY *= camera->mScale;

    if (layer->getZoomValue() == 1.0f) {
        if (camera->computeConstraints(false, layer->getState() != GridLayer::STATE_FULL_SCREEN, firstPosition,
                                       lastPosition)) {
            deltaX = 0.0f;
            mTouchFeedbackDelivered = true;
        }
    }
    mTouchVelX = deltaX * timeElapsedx;
    mTouchVelY = deltaY * timeElapsedx;
    float maxVelXx = (float)mCamera->mWidth * 0.5f;
    float maxVelYx = (float)mCamera->mHeight;
    mTouchVelX = FloatUtils::clamp(mTouchVelX, -maxVelXx, maxVelXx);
    mTouchVelY = FloatUtils::clamp(mTouchVelY, -maxVelYx, maxVelYx);
    mPrevTouchPosX = (float)posX;
    mPrevTouchPosY = (float)posY;
    // The wall should track the finger immediately.
    mCurrentFocusSlot = mTouchMoved ? Shared::INVALID : layer->getSlotIndexForScreenPosition(posX, posY);
    if (!mCamera->isZAnimating()) {
        mCamera->commitMoveInX();
        mCamera->commitMoveInY();
    }
    int anchorSlotIndex = layer->getAnchorSlotIndex(GridLayer::ANCHOR_LEFT);
    const IndexRange &bufferedVisibleRange = layer->getBufferedVisibleRange();
    if (anchorSlotIndex >= bufferedVisibleRange.begin && anchorSlotIndex <= bufferedVisibleRange.end) {
        DisplayItem *item =
            mDisplayItems[(anchorSlotIndex - bufferedVisibleRange.begin) * GridLayer::MAX_ITEMS_PER_SLOT];
        if (item != nullptr && item->mItemRef != nullptr) {
            layer->getHud()->setTimeBarTime(item->mItemRef->mDateTakenInMs);
        }
    }
}

void GridInputProcessor::touchEnded(int posX, int posY, float timeElapsedx) {
    (void)timeElapsedx;
    if (!mProcessTouch || mZoomGesture) {
        return;
    }
    int maxPixelsBeforeSwitch = mCamera->mWidth / 8;
    mCamera->mConvergenceSpeed = 2.0f;
    GridLayer *layer = mLayer;
    if (layer->getExpandedSlot() == Shared::INVALID && !layer->feedAboutToChange() && !mZoomGesture) {
        if (mCurrentSelectedSlot != Shared::INVALID) {
            if (layer->getState() == GridLayer::STATE_FULL_SCREEN) {
                if (!mTouchMoved) {
                    // Tap gesture in fullscreen.
                    if (layer->getZoomValue() == 1.0f) {
                        layer->changeFocusToSlot(mCurrentSelectedSlot, 1.0f);
                    }
                } else if (layer->getZoomValue() == 1.0f) {
                    // Snap to a new slot based on where the drag ended.
                    if (layer->inSlideShowMode()) {
                        layer->endSlideshow();
                    }
                    float deltaX = (float)posX - mFirstTouchPosX;
                    layer->changeFocusToSlot(mCurrentSelectedSlot, 1.0f);
                    HudLayer *hud = layer->getHud();
                    if (deltaX > (float)maxPixelsBeforeSwitch && hud->getMode() != HudLayer::MODE_SELECT) {
                        layer->changeFocusToPreviousSlot(1.0f);
                    } else if (deltaX < (float)-maxPixelsBeforeSwitch && hud->getMode() != HudLayer::MODE_SELECT) {
                        layer->changeFocusToNextSlot(1.0f);
                    }
                } else {
                    // Zoomed in: clamp to the image bounds.
                    bool hitEdge = layer->constrainCameraForSlot(mCurrentSelectedSlot);
                    if (hitEdge && mPrevHitEdge) {
                        float deltaX = (float)posX - mFirstTouchPosX;
                        maxPixelsBeforeSwitch *= 4;
                        mPrevHitEdge = false;
                        HudLayer *hud = layer->getHud();
                        if (deltaX > (float)maxPixelsBeforeSwitch && hud->getMode() != HudLayer::MODE_SELECT) {
                            layer->changeFocusToPreviousSlot(1.0f);
                        } else if (deltaX < (float)-maxPixelsBeforeSwitch && hud->getMode() != HudLayer::MODE_SELECT) {
                            layer->changeFocusToNextSlot(1.0f);
                        } else {
                            mPrevHitEdge = hitEdge;
                        }
                    } else {
                        mPrevHitEdge = hitEdge;
                    }
                }
            }
        } else if (!layer->feedAboutToChange() && layer->getZoomValue() == 1.0f && mTouchMoved) {
            constrainCamera(true);
        }
    }
    mCurrentFocusSlot = Shared::INVALID;
    mCurrentFocusIsPressed = false;
    mPrevTouchPosX = (float)posX;
    mPrevTouchPosY = (float)posY;
    mProcessTouch = false;
}

void GridInputProcessor::constrainCamera(bool b) {
    (void)b;
    GridLayer *layer = mLayer;
    Vector3f firstPosition;
    Vector3f lastPosition;
    Vector3f deltaAnchorPosition;
    deltaAnchorPosition.set(layer->getDeltaAnchorPosition());
    GridCamera *camera = mCamera;
    LayoutInterface *layout = layer->getLayoutInterface();
    GridCameraManager::getSlotPositionForSlotIndex(0, camera, layout, deltaAnchorPosition, firstPosition);
    int lastSlotIndex = layer->getCompleteRange().end;
    GridCameraManager::getSlotPositionForSlotIndex(lastSlotIndex, camera, layout, deltaAnchorPosition, lastPosition);
    camera->computeConstraints(true, layer->getState() != GridLayer::STATE_FULL_SCREEN, firstPosition, lastPosition);
}

void GridInputProcessor::update(float timeElapsed) {
    mDpadIgnoreTime += timeElapsed;
    mGestureDetector.update(SDL_GetTicks());
    if (mCamera->mFriction != 0.0f) {
        constrainCamera(true);
    }
}

void GridInputProcessor::resetScale() {
    mScale = 1.0f;
    mCurrentScaleSlot = Shared::INVALID;
}

// GestureDetector::Listener

bool GridInputProcessor::onDown(const MotionEvent &event) {
    (void)event;
    return true;
}

bool GridInputProcessor::onFling(const MotionEvent &down, const MotionEvent &up, float velocityX, float velocityY) {
    (void)down;
    (void)up;
    (void)velocityY;
    if (mCurrentSelectedSlot != Shared::INVALID) {
        return false;
    }
    mCamera->moveYTo(0.0f);
    mCamera->moveZTo(0.0f);
    mCamera->mConvergenceSpeed = 1.0f;
    mCamera->mFriction = 0.0f;
    float normalizedVelocity = velocityX * mCamera->mOneByScale;
    const IndexRange &visibleRange = mLayer->getVisibleRange();
    int numVisibleSlots = visibleRange.end - visibleRange.begin;
    if (numVisibleSlots > 0) {
        float fastFlingVelocity = 10.0f;
        int slotsToSkip = (int)((float)numVisibleSlots * (-normalizedVelocity / fastFlingVelocity));
        int maxSlots = numVisibleSlots;
        if (slotsToSkip > maxSlots) {
            slotsToSkip = maxSlots;
        }
        if (slotsToSkip < -maxSlots) {
            slotsToSkip = -maxSlots;
        }
        if (std::abs(slotsToSkip) <= 1) {
            if (velocityX > 0.0f) {
                slotsToSkip = -2;
            } else if (velocityX < 0.0f) {
                slotsToSkip = 2;
            }
        }
        int slotToGetTo = mLayer->getAnchorSlotIndex(GridLayer::ANCHOR_CENTER) + slotsToSkip;
        if (slotToGetTo < 0) {
            slotToGetTo = 0;
        }
        int lastSlot = mLayer->getCompleteRange().end;
        if (slotToGetTo > lastSlot) {
            slotToGetTo = lastSlot;
        }
        mLayer->centerCameraForSlot(slotToGetTo, 1.0f);
    }
    constrainCamera(true);
    return true;
}

void GridInputProcessor::onLongPress(const MotionEvent &event) {
    (void)event;
    if (mZoomGesture) {
        return;
    }
    if (mLayer->getFeed() != nullptr && mLayer->getFeed()->isSingleImageMode()) {
        HudLayer *hud = mLayer->getHud();
        hud->getPathBar()->setHidden(true);
        hud->getMenuBar()->setHidden(true);
        if (hud->getMode() != HudLayer::MODE_NORMAL) {
            hud->setMode(HudLayer::MODE_NORMAL);
        }
    }
    if (mCurrentFocusSlot != Shared::INVALID) {
        GridLayer *layer = mLayer;
        if (layer->getState() == GridLayer::STATE_FULL_SCREEN) {
            layer->deselectAll();
        }
        layer->getHud()->enterSelectionMode();
        layer->addSlotToSelectedItems(mCurrentFocusSlot, true, true);
    }
}

bool GridInputProcessor::onScroll(const MotionEvent &down, const MotionEvent &move, float distanceX, float distanceY) {
    (void)down;
    (void)move;
    (void)distanceX;
    (void)distanceY;
    return false;
}

void GridInputProcessor::onShowPress(const MotionEvent &event) {
    (void)event;
}

bool GridInputProcessor::onSingleTapUp(const MotionEvent &event) {
    GridLayer *layer = mLayer;
    int posX = (int)event.getX();
    int posY = (int)event.getY();
    if (mCurrentSelectedSlot != Shared::INVALID) {
        // Fullscreen mode.
        mCamera->mConvergenceSpeed = 2.0f;
        mCamera->mFriction = 0.0f;
        int slotId = mCurrentSelectedSlot;
        if (layer->getZoomValue() == 1.0f) {
            layer->centerCameraForSlot(slotId, 1.0f);
        } else {
            layer->constrainCameraForSlot(slotId);
        }
        DisplayItem *displayItem = layer->getDisplayItemForSlotId(slotId);
        if (displayItem != nullptr) {
            int heightBy2 = mCamera->mHeight / 2;
            bool posYInBounds = std::abs(posY - heightBy2) < 64;
            if (posX < 32 && posYInBounds) {
                layer->changeFocusToPreviousSlot(1.0f);
            } else if (posX > mCamera->mWidth - 32 && posYInBounds) {
                layer->changeFocusToNextSlot(1.0f);
            } else {
                HudLayer *hud = layer->getHud();
                if (layer->inSlideShowMode()) {
                    layer->endSlideshow();
                } else {
                    hud->setAlpha(1.0f - hud->getAlpha());
                }
                if (hud->getMode() == HudLayer::MODE_SELECT) {
                    hud->setAlpha(1.0f);
                }
            }
        }
        return true;
    }

    int slotId = layer->getSlotIndexForScreenPosition(posX, posY);
    if (slotId != Shared::INVALID) {
        HudLayer *hud = layer->getHud();
        if (hud->getMode() == HudLayer::MODE_SELECT) {
            layer->addSlotToSelectedItems(slotId, true, true);
        } else {
            bool centerCamera = (mCurrentSelectedSlot == Shared::INVALID) ? layer->tapGesture(slotId, false) : true;
            if (centerCamera) {
                selectSlot(slotId);
            }
        }
    } else {
        int state = layer->getState();
        if (state != GridLayer::STATE_FULL_SCREEN && state != GridLayer::STATE_GRID_VIEW &&
            layer->getHud()->getMode() != HudLayer::MODE_SELECT) {
            slotId = layer->getMetadataSlotIndexForScreenPosition(posX, posY);
            if (slotId != Shared::INVALID) {
                layer->tapGesture(slotId, true);
            }
        }
    }
    return true;
}

void GridInputProcessor::selectSlot(int slotId) {
    GridLayer *layer = mLayer;
    if (layer->getState() == GridLayer::STATE_GRID_VIEW) {
        DisplayItem *displayItem = layer->getDisplayItemForSlotId(slotId);
        if (displayItem != nullptr) {
            MediaItem *item = displayItem->mItemRef;
            if (item != nullptr && item->getMediaType() == MediaItem::MEDIA_TYPE_VIDEO) {
                // Open videos in the platform player; they do not enter fullscreen photo state.
                if (!FileOperations::openInDefaultApp(item->mFilePath)) {
                    SDL_Log("Could not open %s", item->mFilePath.c_str());
                }
                constrainCamera(true);
                return;
            }
            mCurrentSelectedSlot = slotId;
            layer->endSlideshow();
            layer->setState(GridLayer::STATE_FULL_SCREEN);
            mCamera->mConvergenceSpeed = 2.0f;
            mCamera->mFriction = 0.0f;
            layer->getHud()->fullscreenSelectionChanged(item, mCurrentSelectedSlot + 1,
                                                        layer->getCompleteRange().end + 1);
        }
    }
    constrainCamera(true);
}

bool GridInputProcessor::onDoubleTap(const MotionEvent &event) {
    GridLayer *layer = mLayer;
    if (layer->getState() == GridLayer::STATE_FULL_SCREEN && !mCamera->isZAnimating()) {
        float posX = event.getX() - (float)(mCamera->mWidth / 2);
        float posY = event.getY() - (float)(mCamera->mHeight / 2);
        Vector3f retVal;
        mCamera->convertToRelativeCameraSpace(posX, posY, 0.0f, retVal);
        if (layer->getZoomValue() == 1.0f) {
            layer->setZoomValue(3.0f);
            mCamera->update(0.001f);
            mCamera->moveBy(retVal.x, retVal.y, 0.0f);
            layer->constrainCameraForSlot(mCurrentSelectedSlot);
        } else {
            layer->setZoomValue(1.0f);
        }
        mCamera->mConvergenceSpeed = 2.0f;
        mCamera->mFriction = 0.0f;
        return true;
    }
    return onSingleTapConfirmed(event);
}

bool GridInputProcessor::onDoubleTapEvent(const MotionEvent &event) {
    (void)event;
    return false;
}

bool GridInputProcessor::onSingleTapConfirmed(const MotionEvent &event) {
    (void)event;
    return false;
}

// ScaleGestureDetector::Listener

bool GridInputProcessor::onScale(ScaleGestureDetector *detector) {
    GridLayer *layer = mLayer;
    float scale = detector->getScaleFactor();
    if (std::isinf(scale) || std::isnan(scale)) {
        return true;
    }
    mScale = scale * mScale;
    bool performTranslation = true;
    if (layer->getState() == GridLayer::STATE_FULL_SCREEN) {
        float currentScale = layer->getZoomValue();
        if (currentScale <= 1.0f) {
            performTranslation = false;
        }
        Vector3f retVal;
        if (performTranslation) {
            // Zoom around the focus and pan by its movement to keep pixels under the fingers.
            Vector3f retValCenter;
            Vector3f retValPrev;
            float posX = detector->getFocusX() - (float)(mCamera->mWidth / 2);
            float posY = detector->getFocusY() - (float)(mCamera->mHeight / 2);
            float prevX = detector->getPrevFocusX() - (float)(mCamera->mWidth / 2);
            float prevY = detector->getPrevFocusY() - (float)(mCamera->mHeight / 2);
            mCamera->convertToRelativeCameraSpace(posX, posY, 0.0f, retVal);
            mCamera->convertToRelativeCameraSpace(0.0f, 0.0f, 0.0f, retValCenter);
            mCamera->convertToRelativeCameraSpace(prevX, prevY, 0.0f, retValPrev);
            retVal.x = (retVal.x - retValCenter.x) * (1.0f - 1.0f / scale) + (retValPrev.x - retVal.x);
            retVal.y = (retVal.y - retValCenter.y) * (1.0f - 1.0f / scale) + (retValPrev.y - retVal.y);
        }
        if (currentScale < 0.7f && scale < 1.0f) {
            scale = 1.0f;
        }
        if (currentScale > 8.0f && scale > 1.0f) {
            scale = 1.0f;
        }
        layer->setZoomValue(currentScale * scale);
        if (performTranslation) {
            mCamera->update(0.001f);
            mCamera->moveBy(retVal.x, retVal.y, 0.0f);
            // GridCameraManager handles clamping.
        }
    }
    if (layer->getState() == GridLayer::STATE_GRID_VIEW) {
        mCurrentScaleSlot = Shared::INVALID;
        mCurrentFocusSlot = Shared::INVALID;
    }
    return true;
}

bool GridInputProcessor::onScaleBegin(ScaleGestureDetector *detector) {
    mZoomGesture = true;
    mScale = 1.0f;
    mLayer->getHud()->hideZoomButtons(true);
    int posX = (int)detector->getFocusX();
    int posY = (int)detector->getFocusY();
    int slotId = mLayer->getSlotIndexForScreenPosition(posX, posY);
    if (slotId == Shared::INVALID) {
        slotId = mLayer->getAnchorSlotIndex(GridLayer::ANCHOR_CENTER);
    }
    if (slotId != Shared::INVALID) {
        mCurrentScaleSlot = slotId;
        mCurrentFocusSlot = slotId;
    }
    if (mLayer->getState() == GridLayer::STATE_GRID_VIEW) {
        mCurrentScaleSlot = Shared::INVALID;
        mCurrentFocusSlot = Shared::INVALID;
    }
    constrainCamera(true);
    return true;
}

void GridInputProcessor::onScaleEnd(ScaleGestureDetector *detector, bool cancel) {
    (void)detector;
    GridLayer *layer = mLayer;
    if (!cancel) {
        if (layer->getState() == GridLayer::STATE_FULL_SCREEN) {
            float currentScale = layer->getZoomValue();
            if (currentScale < 1.0f) {
                currentScale = 1.0f;
            } else if (currentScale > 6.0f) {
                currentScale = 6.0f;
            }
            if (currentScale != layer->getZoomValue()) {
                layer->setZoomValue(currentScale);
            }
            layer->constrainCameraForSlot(mCurrentSelectedSlot);
            layer->getHud()->hideZoomButtons(false);
        }
    } else if (layer->getState() == GridLayer::STATE_FULL_SCREEN) {
        layer->setZoomValue(1.0f);
        layer->getHud()->hideZoomButtons(false);
    }
    resetScale();
    mZoomGesture = false;
}

void GridInputProcessor::onSensorChanged(RenderView *view, float x, float y, float z, int state) {
    if (mZoomGesture) {
        return;
    }
    // The caller rotates SDL device axes into display orientation.
    (void)z;
    (void)y;
    const float valueToUse = x;

    // mPrevTiltValueLowPass is never assigned, so the result is one fifth of the reading.
    // This matches the Java tilt response and the surrounding offset constants.
    float tiltValue = 0.8f * mPrevTiltValueLowPass + 0.2f * valueToUse;
    if (std::fabs(tiltValue) < 0.5f) {
        tiltValue = 0.0f;
    }
    if (state == GridLayer::STATE_FULL_SCREEN) {
        tiltValue = 0.0f;
    }
    if (tiltValue != 0.0f && view != nullptr) {
        view->requestRender();
    }
    mCamera->mEyeOffsetX = -3.0f * tiltValue;


}
