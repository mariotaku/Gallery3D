#include "GridDrawManager.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "App.h"
#include "FloatUtils.h"
#include "GridLayer.h"
#include "HudLayer.h"
#include "MediaItem.h"
#include "MediaSet.h"
#include "RenderView.h"
#include "Shared.h"

MediaItemTexture::Config GridDrawManager::sThumbnailConfig;

// ---------------------------------------------------------------------------
// GridDrawManager
// ---------------------------------------------------------------------------

GridDrawManager::GridDrawManager(GridCamera *camera, GridDrawables *drawables, DisplayList *displayList,
                                 DisplayItem **displayItems, DisplaySlot *displaySlots)
    : mDisplayItems(displayItems),
      mDisplaySlots(displaySlots),
      mDisplayList(displayList),
      mCamera(camera),
      mDrawables(drawables) {
    sThumbnailConfig.thumbnailWidth = 128;
    sThumbnailConfig.thumbnailHeight = 96;
    mItemsDrawn.assign((size_t)GridLayer::MAX_ITEMS_DRAWABLE, nullptr);

    StringTexture::Config stc;
    stc.bold = true;
    stc.fontSize = 16 * App::PIXEL_DENSITY;
    stc.sizeMode = StringTexture::Config::SIZE_EXACT;
    stc.overflowMode = StringTexture::Config::OVERFLOW_FADE;
    mNoItemsTexture = std::make_shared<StringTexture>(Res::string::no_items, stc);
}

void GridDrawManager::prepareDraw(const IndexRange &bufferedVisibleRange, const IndexRange &visibleRange,
                                  int selectedSlot, int currentFocusSlot, int currentScaleSlot,
                                  bool currentFocusIsPressed, float spreadValue,
                                  ScaleGestureDetector *scaleGestureDetector, bool holdPosition) {
    mBufferedVisibleRange = bufferedVisibleRange;
    mVisibleRange = visibleRange;
    mSelectedSlot = selectedSlot;
    mCurrentFocusSlot = currentFocusSlot;
    mCurrentFocusIsPressed = currentFocusIsPressed;
    mCurrentScaleSlot = currentScaleSlot;
    mScaleGestureDetector = scaleGestureDetector;
    mSpreadValue = spreadValue;
    mHoldPosition = holdPosition;
}

bool GridDrawManager::update(float timeElapsed) {
    mFocusMixRatio = FloatUtils::animate(mFocusMixRatio, mTargetFocusMixRatio, timeElapsed);
    mTargetFocusMixRatio = 0.0f;
    return mFocusMixRatio != mTargetFocusMixRatio || mSelectedMixRatio.isAnimating();
}

void GridDrawManager::drawThumbnails(RenderView *view, int state) {
    GridDrawables *drawables = mDrawables;
    DisplayList *displayList = mDisplayList;
    DisplayItem **displayItems = mDisplayItems;
    const int firstBufferedVisibleSlot = mBufferedVisibleRange.begin;
    const int lastBufferedVisibleSlot = mBufferedVisibleRange.end;
    const int firstVisibleSlot = mVisibleRange.begin;
    const int lastVisibleSlot = mVisibleRange.end;
    const int selectedSlotIndex = mSelectedSlot;
    const int currentFocusSlot = mCurrentFocusSlot;
    const int currentScaleSlot = mCurrentScaleSlot;

    mItemsDrawn[0] = nullptr;
    int drawnCounter = 0;
    GridQuad *grid = GridDrawables::sGrid;
    grid->bindArrays(view);
    int numTexturesQueued = 0;

    for (int itrSlotIndex = firstBufferedVisibleSlot; itrSlotIndex <= lastBufferedVisibleSlot; ++itrSlotIndex) {
        int index = itrSlotIndex;
        bool priority = !(index < firstVisibleSlot || index > lastVisibleSlot);
        int startSlotIndex = 0;
        const int maxDisplayedItemsPerSlot = (index == mCurrentScaleSlot)
                                                 ? GridLayer::MAX_DISPLAYED_ITEMS_PER_FOCUSED_SLOT
                                                 : GridLayer::MAX_DISPLAYED_ITEMS_PER_SLOT;
        if (index != mCurrentScaleSlot) {
            for (int j = maxDisplayedItemsPerSlot - 1; j >= 0; --j) {
                DisplayItem *displayItem =
                    displayItems[(index - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT + j];
                if (displayItem == nullptr) {
                    continue;
                }
                TexturePtr texture = displayItem->getThumbnailImage(&sThumbnailConfig);
                if (texture && !texture->isLoaded()) {
                    startSlotIndex = j;
                    break;
                }
            }
        }
        // Prime the textures in reverse order.
        for (int j = 0; j < maxDisplayedItemsPerSlot; ++j) {
            int stackIndex = (index == mCurrentScaleSlot) ? (maxDisplayedItemsPerSlot - j - 1) : j;
            DisplayItem *displayItem =
                displayItems[(index - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT + stackIndex];
            if (displayItem == nullptr) {
                continue;
            }
            displayItem->mCurrentSlotIndex = index;
            if (selectedSlotIndex != Shared::INVALID &&
                (index <= selectedSlotIndex - 2 || index >= selectedSlotIndex + 2)) {
                displayItem->clearScreennailImage();
            }
            TexturePtr texture = displayItem->getThumbnailImage(&sThumbnailConfig);
            if (index == mCurrentScaleSlot && texture && !texture->isLoaded()) {
                view->prime(texture, true);
                view->bind(texture);
            } else if (texture && !texture->isLoaded() && numTexturesQueued <= 6) {
                bool isCached = texture->isCached();
                view->prime(texture, priority);
                view->bind(texture);
                if (priority && isCached && texture->mState != Texture::STATE_ERROR) {
                    ++numTexturesQueued;
                }
            }
        }
        if (itrSlotIndex == selectedSlotIndex) {
            continue;
        }
        view->prime(drawables->mTexturePlaceholder, true);
        TexturePtr placeholder = (state == GridLayer::STATE_GRID_VIEW) ? drawables->mTexturePlaceholder : nullptr;
        const bool pushDown = !(state == GridLayer::STATE_GRID_VIEW || state == GridLayer::STATE_FULL_SCREEN);

        for (int j = 0; j < GridLayer::MAX_ITEMS_PER_SLOT; ++j) {
            DisplayItem *displayItem =
                displayItems[(index - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT + j];
            if (displayItem == nullptr) {
                continue;
            }
            TexturePtr texture = displayItem->getThumbnailImage(&sThumbnailConfig);
            if (!texture || !texture->isLoaded()) {
                if (currentScaleSlot != index) {
                    if (j == 0) {
                        MediaSet *parentSet = displayItem->mItemRef ? displayItem->mItemRef->mParentMediaSet : nullptr;
                        if (parentSet != nullptr && parentSet->getNumItems() <= 1) {
                            displayList->setAlive(displayItem, false);
                        }
                    } else {
                        displayList->setAlive(displayItem, false);
                    }
                }
            }
            const float dx1 = mScaleGestureDetector->getTopFingerDeltaX();
            const float dy1 = mScaleGestureDetector->getTopFingerDeltaY();
            const float dx2 = mScaleGestureDetector->getBottomFingerDeltaX();
            const float dy2 = mScaleGestureDetector->getBottomFingerDeltaY();
            const float span = mScaleGestureDetector->getCurrentSpan();
            if (state == GridLayer::STATE_FULL_SCREEN) {
                displayList->setOffset(displayItem, false, true, span, dx1, dy1, dx2, dy2);
            } else if (!mHoldPosition) {
                if (state != GridLayer::STATE_GRID_VIEW) {
                    if (currentScaleSlot == index) {
                        displayList->setOffset(displayItem, true, false, span, dx1, dy1, dx2, dy2);
                    } else if (currentScaleSlot != Shared::INVALID) {
                        displayList->setOffset(displayItem, true, true, span, dx1, dy1, dx2, dy2);
                    } else {
                        displayList->setOffset(displayItem, false, false, span, dx1, dy1, dx2, dy2);
                    }
                } else {
                    float minVal = -1.0f;
                    float maxVal = GridCamera::EYE_Z * 0.5f;
                    float zVal = minVal + mSpreadValue;
                    zVal = FloatUtils::clamp(zVal, minVal, maxVal);
                    if (std::isinf(zVal) || std::isnan(zVal)) {
                        mCamera->moveZTo(0.0f);
                    } else {
                        mCamera->moveZTo(-zVal);
                    }
                }
            }
        }

        for (int j = startSlotIndex; j < GridLayer::MAX_ITEMS_PER_SLOT; ++j) {
            DisplayItem *displayItem =
                displayItems[(index - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT + j];
            if (displayItem == nullptr) {
                break;
            }
            if (currentFocusSlot == index) {
                displayList->setHasFocus(displayItem, true, pushDown);
                mTargetFocusMixRatio = 1.0f;
            } else {
                displayList->setHasFocus(displayItem, false, pushDown);
            }
            if (j >= maxDisplayedItemsPerSlot) {
                continue;
            }
            TexturePtr texture = displayItem->getThumbnailImage(&sThumbnailConfig);
            if (!texture) {
                // Move on to the next stack.
                break;
            }
            if (index == mCurrentScaleSlot) {
                displayItem->mAlive = true;
            }
            if ((!displayItem->isAnimating() || !texture->isLoaded()) &&
                displayItem->getStackIndex() > GridLayer::MAX_ITEMS_PER_SLOT) {
                displayList->setAlive(displayItem, true);
                continue;
            }
            if (index < firstVisibleSlot || index > lastVisibleSlot) {
                if (view->bind(texture)) {
                    displayList->setAlive(displayItem, true);
                }
                continue;
            }
            drawDisplayItem(view, displayItem, texture, PASS_THUMBNAIL_CONTENT, placeholder,
                            displayItem->mAnimatedPlaceholderFade);

            if (drawnCounter >= GridLayer::MAX_ITEMS_DRAWABLE - 1 || drawnCounter < 0) {
                break;
            }
            mItemsDrawn[(size_t)drawnCounter++] = displayItem;
            mItemsDrawn[(size_t)drawnCounter] = nullptr;
        }
    }
    mDrawnCounter = drawnCounter;
    grid->unbindArrays(view);
}

void GridDrawManager::drawFocusItems(RenderView *view, float zoomValue, bool slideshowMode,
                                     float timeElapsedSinceView) {
    int selectedSlotIndex = mSelectedSlot;
    GridDrawables *drawables = mDrawables;
    GridCamera *camera = mCamera;
    DisplayItem **displayItems = mDisplayItems;
    int firstBufferedVisibleSlot = mBufferedVisibleRange.begin;
    int lastBufferedVisibleSlot = mBufferedVisibleRange.end;
    bool isCameraZAnimating = camera->isZAnimating();

    for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
        if (selectedSlotIndex != Shared::INVALID && (i >= selectedSlotIndex - 2 && i <= selectedSlotIndex + 2)) {
            continue;
        }
        DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
        if (displayItem != nullptr) {
            displayItem->clearScreennailImage();
        }
    }
    if (selectedSlotIndex == Shared::INVALID) {
        return;
    }

    float camX = camera->mLookAtX * camera->mScale;
    int centerIndexInDrawnArray = (selectedSlotIndex - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT;
    if (centerIndexInDrawnArray < 0 || centerIndexInDrawnArray >= GridLayer::MAX_ITEMS_DRAWABLE) {
        return;
    }
    DisplayItem *centerDisplayItem = displayItems[centerIndexInDrawnArray];
    if (centerDisplayItem == nullptr || centerDisplayItem->mItemRef == nullptr ||
        centerDisplayItem->mItemRef->mId == Shared::INVALID) {
        return;
    }
    bool focusItemTextureLoaded = false;
    TexturePtr centerTexture = centerDisplayItem->getScreennailImage();
    if (centerTexture && centerTexture->isLoaded()) {
        focusItemTextureLoaded = true;
    }
    float centerTranslateX = centerDisplayItem->mAnimatedPosition.x;
    const bool skipPrevious = centerTranslateX < camX;
    view->setAlpha(1.0f);
    view->enableBlend(true);
    view->blendFunc(GL_ONE, GL_ONE);
    float backupImageTheta = 0.0f;

    for (int i = -1; i <= 1; ++i) {
        if (slideshowMode && timeElapsedSinceView > 1.0f && i != 0) {
            continue;
        }
        float viewAspect = camera->mAspectRatio;
        int selectedSlotToUse = selectedSlotIndex + i;
        if (selectedSlotToUse < 0 || selectedSlotToUse > lastBufferedVisibleSlot) {
            continue;
        }
        int indexInDrawnArray = (selectedSlotToUse - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT;
        if (indexInDrawnArray < 0 || indexInDrawnArray >= GridLayer::MAX_ITEMS_DRAWABLE) {
            return;
        }
        DisplayItem *displayItem = displayItems[indexInDrawnArray];
        if (displayItem == nullptr) {
            continue;
        }
        MediaItem *item = displayItem->mItemRef;
        const TexturePtr thumbnailTexture = displayItem->getThumbnailImage(&sThumbnailConfig);
        TexturePtr texture = displayItem->getScreennailImage();
        if (isCameraZAnimating && (!texture || !texture->isLoaded())) {
            texture = thumbnailTexture;
            mSelectedMixRatio.setValue(0.0f);
            mSelectedMixRatio.animateValue(1.0f, 0.75f, view->getFrameTime());
        }
        // Zoomed in, the picture wants more pixels than the screennail has.
        // Where the source can crop, that is the tiled path below and nothing
        // whole is fetched at all. Where it cannot - a photo on this disk,
        // since SDL_image decodes a whole file or none of it - the picture is
        // loaded once more at a higher resolution, which is what this was
        // before tiles existed and still is for local albums.
        const bool tiled = TiledImage::canTile(item);
        TexturePtr hiRes =
            (!tiled && zoomValue != 1.0f && i == 0 && item->getMediaType() != MediaItem::MEDIA_TYPE_VIDEO)
                ? displayItem->getHiResImage()
                : nullptr;
        // The original swapped the hi-res texture out for the screennail above
        // density 1: on a dense handset the screennail already held everything
        // the screen could resolve. This window is far larger than the phone
        // that was written for, so a zoom has somewhere to go and the extra
        // texture earns its memory.
        if (i != 0) {
            displayItem->clearHiResImage();
        }
        if (hiRes) {
            if (!hiRes->isLoaded()) {
                view->bind(hiRes);
                view->prime(hiRes, true);
            } else {
                texture = hiRes;
            }
        }
        const TexturePtr fsTexture = texture;
        if (!texture || !texture->isLoaded()) {
            // Near enough to the middle that this is the photo being looked at
            // rather than one being swiped past.
            //
            // The original compared against a tenth of a unit. That is a tenth
            // of a pixel here, because both sides are in the layout's pixels,
            // and the camera settles about a third of one away from the item it
            // is centred on. So the test never passed, the screennail was never
            // asked for, and the fullscreen view had been showing the grid
            // thumbnail until the separate full resolution texture arrived.
            // Measured against the item instead, which is what "centred" was
            // always about.
            const float centred = (float)camera->mItemWidth * 0.02f;
            if (std::fabs(centerTranslateX - camX) < centred) {
                if (focusItemTextureLoaded && i != 0) {
                    view->bind(texture);
                }
                if (i == 0) {
                    view->bind(texture);
                    view->prime(texture, true);
                }
            }
            texture = thumbnailTexture;
            if (i == 0) {
                mSelectedMixRatio.setValue(0.0f);
                mSelectedMixRatio.animateValue(1.0f, 0.75f, view->getFrameTime());
            }
        }
        if (camera->isAnimating() || slideshowMode) {
            if (!slideshowMode && skipPrevious && i == -1) {
                continue;
            }
            if (!skipPrevious && i == 1) {
                continue;
            }
        }
        int theta = (int)displayItem->getImageTheta();
        // In slideshow mode the previous item is drawn where the next one goes.
        if (slideshowMode && timeElapsedSinceView < 1.0f && timeElapsedSinceView != 0.0f) {
            if (i == -1) {
                int nextSlotToUse = selectedSlotToUse + 1;
                if (nextSlotToUse >= 0 && nextSlotToUse <= lastBufferedVisibleSlot) {
                    int nextIndexInDrawnArray =
                        (nextSlotToUse - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT;
                    if (nextIndexInDrawnArray >= 0 && nextIndexInDrawnArray < GridLayer::MAX_ITEMS_DRAWABLE &&
                        displayItems[nextIndexInDrawnArray] != nullptr) {
                        float currentImageTheta = displayItem->mAnimatedImageTheta;
                        displayItem = displayItems[nextIndexInDrawnArray];
                        backupImageTheta = displayItem->mAnimatedImageTheta;
                        displayItem->mAnimatedImageTheta = currentImageTheta;
                        view->setAlpha(1.0f - timeElapsedSinceView);
                    }
                }
            } else if (i == 0) {
                displayItem->mAnimatedImageTheta = backupImageTheta;
                view->setAlpha(timeElapsedSinceView);
            }
        }
        if (!texture) {
            continue;
        }

        int vboIndex = i + 1;
        float alpha = view->getAlpha();
        float selectedMixRatio = mSelectedMixRatio.getValue(view->getFrameTime());
        if (selectedMixRatio != 1.0f) {
            texture = thumbnailTexture;
            view->setAlpha(alpha * (1.0f - selectedMixRatio));
        }
        if (!texture) {
            view->setAlpha(alpha);
            continue;
        }
        GridQuad *quad = GridDrawables::sFullscreenGrid[vboIndex];
        float u = texture->getNormalizedWidth();
        float v = texture->getNormalizedHeight();
        float imageWidth = (float)texture->getWidth();
        float imageHeight = (float)texture->getHeight();
        bool portrait = ((theta / 90) % 2 == 1);
        if (portrait) {
            viewAspect = 1.0f / viewAspect;
        }
        quad->resizeQuad(viewAspect, u, v, imageWidth, imageHeight);
        quad->bindArrays(view);
        drawDisplayItem(view, displayItem, texture, PASS_FOCUS_CONTENT, nullptr, 0.0f);
        quad->unbindArrays(view);

        // Over the top of it, the parts of the original that are on screen.
        // Only for the picture being looked at, and only once it is settled on
        // the screennail: during the cross fade from the thumbnail the quad is
        // still being resized under it, and tiles placed against a quad that is
        // about to change would sit off the picture for a frame.
        if (i == 0 && zoomValue != 1.0f && selectedMixRatio == 1.0f && !slideshowMode) {
            drawFocusTiles(view, displayItem, quad);
        }

        if (selectedMixRatio != 0.0f && selectedMixRatio != 1.0f && fsTexture) {
            view->setAlpha(alpha * selectedMixRatio);
            u = fsTexture->getNormalizedWidth();
            v = fsTexture->getNormalizedHeight();
            imageWidth = (float)fsTexture->getWidth();
            imageHeight = (float)fsTexture->getHeight();
            quad->resizeQuad(viewAspect, u, v, imageWidth, imageHeight);
            quad->bindArrays(view);
            drawDisplayItem(view, displayItem, fsTexture, PASS_FOCUS_CONTENT, nullptr, 1.0f);
            quad->unbindArrays(view);
        }
        if (i == 0 || slideshowMode) {
            mCurrentFocusItemWidth = quad->getWidth();
            mCurrentFocusItemHeight = quad->getHeight();
            if (portrait) {
                std::swap(mCurrentFocusItemWidth, mCurrentFocusItemHeight);
            }
        }
        view->setAlpha(alpha);
        if (item->getMediaType() == MediaItem::MEDIA_TYPE_VIDEO) {
            // The play graphic overlay.
            GridDrawables::sVideoGrid->bindArrays(view);
            drawDisplayItem(view, displayItem, drawables->mTextureVideo, PASS_VIDEO_LABEL, nullptr, 0.0f);
            GridDrawables::sVideoGrid->unbindArrays(view);
        }
    }
}

void GridDrawManager::drawFocusTiles(RenderView *view, DisplayItem *displayItem, GridQuad *quad) {
    TiledImage *tiled = displayItem->getTiledImage();
    if (tiled == nullptr || quad == nullptr) {
        return;
    }
    // A rotated picture is drawn through the quad's own rotation, and the tile
    // rectangles are worked out in screen axes. Squaring those two is work for
    // no one: nothing the museum serves is rotated, and a rotated local photo
    // has no tiles to draw anyway.
    if (displayItem->mAnimatedImageTheta != 0.0f || displayItem->mAnimatedTheta != 0.0f) {
        return;
    }
    GridQuad *tileQuad = GridDrawables::sTileGrid;
    if (tileQuad == nullptr) {
        return;
    }

    const float quadWidth = quad->getWidth();
    const float quadHeight = quad->getHeight();
    if (quadWidth <= 0.0f || quadHeight <= 0.0f) {
        return;
    }

    GridCamera *camera = mCamera;
    // What the screen covers, in the same space the item's position is in. Both
    // corners come from the camera, so this is the on screen rectangle
    // expressed in world units: x to the right, y down.
    Vector3f topLeft;
    Vector3f bottomRight;
    camera->convertToCameraSpace(0.0f, 0.0f, 0.0f, topLeft);
    camera->convertToCameraSpace((float)camera->mWidth, (float)camera->mHeight, 0.0f, bottomRight);
    const float viewSpan = bottomRight.x - topLeft.x;
    if (viewSpan <= 0.0f) {
        return;
    }

    const float centerX = displayItem->mAnimatedPosition.x * camera->mOneByScale;
    const float centerY = displayItem->mAnimatedPosition.y * camera->mOneByScale;
    const float pictureLeft = centerX - quadWidth * 0.5f;
    const float pictureTop = centerY - quadHeight * 0.5f;

    // The visible part of the picture, as fractions of it.
    const float left = (topLeft.x - pictureLeft) / quadWidth;
    const float right = (bottomRight.x - pictureLeft) / quadWidth;
    const float top = (topLeft.y - pictureTop) / quadHeight;
    const float bottom = (bottomRight.y - pictureTop) / quadHeight;
    if (right <= 0.0f || left >= 1.0f || bottom <= 0.0f || top >= 1.0f) {
        return;
    }

    // How wide the whole picture is drawn, in screen pixels. That is what says
    // how much detail is worth fetching, and it is the zoom in the only form
    // this needs it.
    const float drawnWidth = (quadWidth / viewSpan) * (float)camera->mWidth;
    tiled->update(view, left, top, right, bottom, drawnWidth);

    const std::vector<TiledImage::Placed> &tiles = tiled->placedTiles();
    if (tiles.empty()) {
        return;
    }

    // The focus pass draws additively over a cleared screen, which suits one
    // layer and not two. A tile goes over the screennail rather than adding to
    // it, so for this pass alone the blend is the ordinary one. Everything a
    // tile carries is opaque, so it replaces what it covers, and where no tile
    // has arrived the screennail underneath still shows.
    view->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The same transform drawDisplayItem puts the whole picture under, so the
    // tiles land in its local space.
    const float translateZ = -displayItem->mAnimatedPosition.z;
    view->glTranslatef(-centerX, -centerY, -translateZ);
    for (const TiledImage::Placed &tile : tiles) {
        if (!tile.texture || !tile.texture->isLoaded()) {
            continue;
        }
        // Local axes, not screen ones: the quad's +x is screen left and its +y
        // is screen up, so a fraction measured from the picture's left or top
        // counts down from the positive corner. The same convention the whole
        // picture is drawn under, which is why its rightmost vertex carries the
        // texture coordinate zero.
        const float xMin = quadWidth * (0.5f - tile.right);
        const float xMax = quadWidth * (0.5f - tile.left);
        const float yMin = quadHeight * (0.5f - tile.bottom);
        const float yMax = quadHeight * (0.5f - tile.top);
        const float uMax = tile.texture->getNormalizedWidth();
        const float vMax = tile.texture->getNormalizedHeight();
        tileQuad->setCorners(xMin, yMin, xMax, yMax, uMax, vMax, 0.0f, 0.0f);
        tileQuad->bindArrays(view);
        if (view->bind(tile.texture)) {
            GridQuad::draw(view, 0.0f);
        }
        tileQuad->unbindArrays(view);
    }
    view->glTranslatef(centerX, centerY, translateZ);
    view->blendFunc(GL_ONE, GL_ONE);
}

void GridDrawManager::drawBlendedComponents(RenderView *view, float alpha, int state, int hudMode,
                                            float stackMixRatio, float gridMixRatio,
                                            MediaBucketList &selectedBucketList, MediaBucketList &markedBucketList,
                                            bool isFeedLoading) {
    (void)alpha;
    int firstBufferedVisibleSlot = mBufferedVisibleRange.begin;
    int lastBufferedVisibleSlot = mBufferedVisibleRange.end;
    int firstVisibleSlot = mVisibleRange.begin;
    int lastVisibleSlot = mVisibleRange.end;
    DisplayItem **displayItems = mDisplayItems;
    GridDrawables *drawables = mDrawables;

    if (state == GridLayer::STATE_FULL_SCREEN) {
        return;
    }

    // The frames around the drawn items.
    bool currentFocusIsPressed = mCurrentFocusIsPressed;
    GridDrawables::sFrame->bindArrays(view);
    TexturePtr texturePlaceHolder =
        (state == GridLayer::STATE_GRID_VIEW) ? drawables->mTextureGridFrame : drawables->mTextureFrame;
    for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
        if (i < firstVisibleSlot || i > lastVisibleSlot) {
            continue;
        }
        bool slotIsAlive = false;
        const int maxDisplayedItemsPerSlot = (i == mCurrentScaleSlot)
                                                 ? GridLayer::MAX_DISPLAYED_ITEMS_PER_FOCUSED_SLOT
                                                 : GridLayer::MAX_DISPLAYED_ITEMS_PER_SLOT;
        if (state != GridLayer::STATE_MEDIA_SETS && state != GridLayer::STATE_TIMELINE) {
            for (int j = 0; j < maxDisplayedItemsPerSlot; ++j) {
                DisplayItem *displayItem =
                    displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT + j];
                if (displayItem != nullptr) {
                    slotIsAlive |= displayItem->mAlive;
                }
            }
            if (!slotIsAlive) {
                DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
                if (displayItem != nullptr) {
                    drawDisplayItem(view, displayItem, texturePlaceHolder, PASS_FRAME_PLACEHOLDER, nullptr, 0.0f);
                }
            }
        }
    }

    TexturePtr texturePressed = drawables->mTextureFramePressed;
    TexturePtr textureFocus = drawables->mTextureFrameFocus;
    TexturePtr textureGrid = drawables->mTextureGridFrame;
    TexturePtr texture = drawables->mTextureFrame;

    int drawnCounter = mDrawnCounter;
    if (texture && drawnCounter > 0) {
        std::sort(mItemsDrawn.begin(), mItemsDrawn.begin() + drawnCounter,
                  [](const DisplayItem *a, const DisplayItem *b) {
                      if (a == nullptr || b == nullptr) {
                          return false;
                      }
                      return a->mAnimatedPosition.z < b->mAnimatedPosition.z;
                  });
        float timeElapsedSinceGridView = gridMixRatio;
        float timeElapsedSinceStackView = stackMixRatio;
        for (int i = drawnCounter - 1; i >= 0; --i) {
            DisplayItem *itemDrawn = mItemsDrawn[(size_t)i];
            if (itemDrawn == nullptr) {
                continue;
            }
            bool inSelected = selectedBucketList.find(itemDrawn->mItemRef);
            bool inMarked = markedBucketList.find(itemDrawn->mItemRef);
            TexturePtr previousTexture = inSelected ? texturePressed : texture;
            TexturePtr textureToUse = itemDrawn->getHasFocus()
                                          ? (currentFocusIsPressed ? texturePressed : textureFocus)
                                          : (inSelected ? texturePressed : (inMarked ? texture : textureGrid));
            float ratio = timeElapsedSinceGridView;
            if (!itemDrawn->mAlive) {
                continue;
            }
            if (state != GridLayer::STATE_GRID_VIEW) {
                previousTexture = inSelected ? texturePressed : texture;
                textureToUse =
                    itemDrawn->getHasFocus() ? (currentFocusIsPressed ? texturePressed : textureFocus) : previousTexture;
                if (timeElapsedSinceStackView == 1.0f) {
                    ratio = mFocusMixRatio;
                } else {
                    ratio = timeElapsedSinceStackView;
                    previousTexture = textureGrid;
                }
            }
            drawDisplayItem(view, itemDrawn, textureToUse, PASS_FRAME, previousTexture, ratio);
        }
    }
    GridDrawables::sFrame->unbindArrays(view);

    if (mSpreadValue <= 1.0f) {
        view->depthFunc(GL_ALWAYS);
    }
    if (state == GridLayer::STATE_MEDIA_SETS || state == GridLayer::STATE_TIMELINE) {
        DisplaySlot *displaySlots = mDisplaySlots;
        GridDrawables::sTextGrid->bindArrays(view);
        const float textOffsetY = 0.82f;
        view->glTranslatef(0.0f, -textOffsetY, 0.0f);
        auto &stringTextureTable = GridDrawables::sStringTextureTable;

        bool itemsPresent = false;
        for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
            itemsPresent = true;
            if (mSpreadValue > 1.0f) {
                continue;
            }
            DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
            if (displayItem == nullptr) {
                continue;
            }
            DisplaySlot &displaySlot = displaySlots[i - firstBufferedVisibleSlot];
            TexturePtr textureString = displaySlot.getTitleImage(stringTextureTable);
            view->loadTexture(textureString);
            if (textureString) {
                if (i < firstVisibleSlot || i > lastVisibleSlot) {
                    continue;
                }
                drawDisplayItem(view, displayItem, textureString, PASS_TEXT_LABEL, nullptr, 0.0f);
            }
        }

        if (!itemsPresent && !isFeedLoading) {
            GridDrawables::sTextGrid->unbindArrays(view);
            int wWidth = view->getWidth();
            int wHeight = view->getHeight();
            // Size this to be 40 pixels narrower than the window.
            mNoItemsTexture->mWidth = wWidth - 40;
            view->loadTexture(mNoItemsTexture);
            float x = std::floor((float)(wWidth / 2) - (float)mNoItemsTexture->getWidth() / 2.0f);
            float y = std::floor((float)(wHeight / 2) - (float)mNoItemsTexture->getHeight() / 2.0f);
            view->draw2D(mNoItemsTexture, x, y);
            GridDrawables::sTextGrid->bindArrays(view);
        }

        float yLocOffset = 0.2f;
        view->glTranslatef(0.0f, -yLocOffset, 0.0f);
        for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
            if (mSpreadValue > 1.0f) {
                continue;
            }
            DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
            if (displayItem == nullptr) {
                continue;
            }
            DisplaySlot &displaySlot = displaySlots[i - firstBufferedVisibleSlot];
            TexturePtr textureString = displaySlot.getLocationImage(stringTextureTable);
            if (textureString) {
                view->loadTexture(textureString);
                drawDisplayItem(view, displayItem, textureString, PASS_TEXT_LABEL, nullptr, 0.0f);
            }
        }

        if (state == GridLayer::STATE_TIMELINE) {
            GridDrawables::sLocationGrid->bindArrays(view);
            TexturePtr locationTexture = drawables->mTextureLocation;
            const float yLocationLabelOffset = 0.19f;
            for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
                if (mCurrentScaleSlot != Shared::INVALID) {
                    continue;
                }
                DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
                if (displayItem == nullptr || !displayItem->mAlive) {
                    continue;
                }
                DisplaySlot &displaySlot = displaySlots[i - firstBufferedVisibleSlot];
                if (!displaySlot.hasValidLocation()) {
                    continue;
                }
                std::shared_ptr<StringTexture> textureString = displaySlot.getLocationImage(stringTextureTable);
                float textWidth = textureString ? textureString->computeTextWidth() : 0.0f;
                textWidth *= (mCamera->mOneByScale * 0.5f);
                if (textWidth == 0.0f) {
                    textWidth -= 0.18f;
                }
                textWidth += 0.1f;
                view->glTranslatef(textWidth, -yLocationLabelOffset, 0.0f);
                drawDisplayItem(view, displayItem, locationTexture, PASS_LOCATION_LABEL, nullptr, 0.0f);
                view->glTranslatef(-textWidth, yLocationLabelOffset, 0.0f);
            }
            GridDrawables::sLocationGrid->unbindArrays(view);
        } else if (state == GridLayer::STATE_MEDIA_SETS && stackMixRatio > 0.0f) {
            GridDrawables::sSourceIconGrid->bindArrays(view);
            TexturePtr transparentTexture = drawables->mTextureTransparent;
            for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
                if (mCurrentScaleSlot != Shared::INVALID) {
                    continue;
                }
                DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
                if (displayItem == nullptr || !displayItem->mAlive) {
                    continue;
                }
                DisplaySlot &displaySlot = displaySlots[i - firstBufferedVisibleSlot];
                TexturePtr locationTexture =
                    view->getResource(drawables->getIconForSet(displaySlot.getMediaSet(), false), false);
                // The icon sits at 0.85 alpha over the top item in the stack.
                view->glTranslatef(0.24f, 0.5f, 0.0f);
                drawDisplayItem(view, displayItem, locationTexture, PASS_MEDIASET_SOURCE_LABEL, transparentTexture,
                                0.85f);
                view->glTranslatef(-0.24f, -0.5f, 0.0f);
            }
            GridDrawables::sSourceIconGrid->unbindArrays(view);
        }
        view->glTranslatef(0.0f, yLocOffset, 0.0f);
        view->glTranslatef(0.0f, textOffsetY, 0.0f);
        GridDrawables::sTextGrid->unbindArrays(view);
    }

    if (hudMode == HudLayer::MODE_SELECT) {
        TexturePtr textureSelectedOn = drawables->mTextureCheckmarkOn;
        TexturePtr textureSelectedOff = drawables->mTextureCheckmarkOff;
        view->prime(textureSelectedOn, true);
        view->prime(textureSelectedOff, true);
        GridDrawables::sSelectedGrid->bindArrays(view);
        for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
            DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
            if (displayItem != nullptr) {
                TexturePtr textureToUse =
                    selectedBucketList.find(displayItem->mItemRef) ? textureSelectedOn : textureSelectedOff;
                drawDisplayItem(view, displayItem, textureToUse, PASS_SELECTION_LABEL, nullptr, 0.0f);
            }
        }
        GridDrawables::sSelectedGrid->unbindArrays(view);
    }

    GridDrawables::sVideoGrid->bindArrays(view);
    TexturePtr videoTexture = drawables->mTextureVideo;
    for (int i = firstBufferedVisibleSlot; i <= lastBufferedVisibleSlot; ++i) {
        DisplayItem *displayItem = displayItems[(i - firstBufferedVisibleSlot) * GridLayer::MAX_ITEMS_PER_SLOT];
        if (displayItem != nullptr && displayItem->mAlive && displayItem->mItemRef != nullptr &&
            displayItem->mItemRef->getMediaType() == MediaItem::MEDIA_TYPE_VIDEO) {
            drawDisplayItem(view, displayItem, videoTexture, PASS_VIDEO_LABEL, nullptr, 0.0f);
        }
    }
    GridDrawables::sVideoGrid->unbindArrays(view);
    view->depthFunc(GL_LEQUAL);
}

void GridDrawManager::drawDisplayItem(RenderView *view, DisplayItem *displayItem, const TexturePtr &texture, int pass,
                                      const TexturePtr &previousTexture, float mixRatio) {
    if (!texture) {
        return;
    }
    GridCamera *camera = mCamera;
    const Vector3f &animatedPosition = displayItem->mAnimatedPosition;
    float translateXf = animatedPosition.x * camera->mOneByScale;
    float translateYf = animatedPosition.y * camera->mOneByScale;
    float translateZf = -animatedPosition.z;
    int stackId = displayItem->getStackIndex();
    const int maxDisplayedItemsPerSlot =
        (displayItem->mCurrentSlotIndex == mCurrentScaleSlot && mCurrentScaleSlot != Shared::INVALID)
            ? GridLayer::MAX_DISPLAYED_ITEMS_PER_FOCUSED_SLOT
            : GridLayer::MAX_DISPLAYED_ITEMS_PER_SLOT;

    if (pass == PASS_PLACEHOLDER || pass == PASS_FRAME_PLACEHOLDER) {
        translateZf = -0.04f;
    } else {
        if (pass == PASS_FRAME) {
            translateZf += 0.02f;
        }
        if ((pass == PASS_TEXT_LABEL || pass == PASS_LOCATION_LABEL || pass == PASS_SELECTION_LABEL) &&
            !displayItem->isAlive()) {
            translateZf = 0.0f;
        }
        if (pass == PASS_TEXT_LABEL && translateZf > 0.0f) {
            translateZf = 0.0f;
        }
    }

    bool usingMixedTextures = false;
    bool bound = false;
    if ((pass != PASS_THUMBNAIL_CONTENT) ||
        (stackId < maxDisplayedItemsPerSlot && texture->isLoaded() &&
         (!previousTexture || previousTexture->isLoaded()))) {
        if (mixRatio == 1.0f || !previousTexture || texture == previousTexture) {
            bound = view->bind(texture);
        } else if (mixRatio != 0.0f) {
            if (!texture->isLoaded() || !previousTexture->isLoaded()) {
                // Submit the previous texture to the load queue.
                view->bind(previousTexture);
                bound = view->bind(texture);
            } else {
                usingMixedTextures = true;
                bound = view->bindMixed(previousTexture, texture, mixRatio);
            }
        } else {
            bound = view->bind(previousTexture);
        }
    } else if (stackId >= maxDisplayedItemsPerSlot && pass == PASS_THUMBNAIL_CONTENT) {
        mDisplayList->setAlive(displayItem, true);
    }

    if (!texture->isLoaded() || !bound) {
        if (pass == PASS_THUMBNAIL_CONTENT) {
            if (previousTexture && previousTexture->isLoaded() && translateZf == 0.0f) {
                translateZf = -0.08f;
                bound = view->bind(previousTexture) || bound;
            }
            if (!bound) {
                return;
            }
        } else {
            return;
        }
    } else if (pass == PASS_THUMBNAIL_CONTENT || pass == PASS_FOCUS_CONTENT) {
        if (!displayItem->mAlive) {
            mDisplayList->setAlive(displayItem, true);
        }
    }

    view->glTranslatef(-translateXf, -translateYf, -translateZf);
    float theta = (pass == PASS_FOCUS_CONTENT)
                      ? (displayItem->mAnimatedImageTheta + displayItem->mAnimatedTheta)
                      : displayItem->mAnimatedTheta;
    if (theta != 0.0f) {
        view->glRotatef(theta, 0.0f, 0.0f, 1.0f);
    }
    float orientation = 0.0f;
    if (pass == PASS_THUMBNAIL_CONTENT && displayItem->mAnimatedImageTheta != 0.0f) {
        orientation = displayItem->mAnimatedImageTheta;
    }
    if (pass == PASS_FRAME || pass == PASS_FRAME_PLACEHOLDER) {
        GridQuadFrame::draw(view);
    } else {
        GridQuad::draw(view, orientation);
    }
    if (theta != 0.0f) {
        view->glRotatef(-theta, 0.0f, 0.0f, 1.0f);
    }
    view->glTranslatef(translateXf, translateYf, translateZf);
    if (usingMixedTextures) {
        view->unbindMixed();
    }
}
