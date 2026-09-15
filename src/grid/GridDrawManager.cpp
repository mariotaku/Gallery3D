#include "grid/GridDrawManager.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "app/App.h"
#include "core/FloatUtils.h"
#include "grid/GridLayer.h"
#include "hud/HudLayer.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "graphics/RenderView.h"
#include "core/Shared.h"

MediaItemTexture::Config GridDrawManager::sThumbnailConfig;

// GridDrawManager

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
    // The box scales with the font. At a fixed 32 pixels, a denser screen's
    // text is taller than the box and loses its descenders.
    stc.height = (int)std::ceil(32.0f * App::PIXEL_DENSITY);
    stc.sizeMode = StringTexture::Config::SIZE_EXACT;
    stc.overflowMode = StringTexture::Config::OVERFLOW_FADE;
    mNoItemsTexture = std::make_shared<StringTexture>(Res::string::no_items, stc);
}

void GridDrawManager::prepareDraw(const IndexRange &bufferedVisibleRange, const IndexRange &visibleRange,
                                  int selectedSlot, int currentFocusSlot, int currentScaleSlot,
                                  bool currentFocusIsPressed, int hoverSlot, float spreadValue,
                                  ScaleGestureDetector *scaleGestureDetector, bool holdPosition) {
    mBufferedVisibleRange = bufferedVisibleRange;
    mVisibleRange = visibleRange;
    mSelectedSlot = selectedSlot;
    mCurrentFocusSlot = currentFocusSlot;
    mCurrentFocusIsPressed = currentFocusIsPressed;
    mHoverSlot = hoverSlot;
    mCurrentScaleSlot = currentScaleSlot;
    mScaleGestureDetector = scaleGestureDetector;
    mSpreadValue = spreadValue;
    mHoldPosition = holdPosition;
}

TexturePtr GridDrawManager::thumbnailOf(DisplayItem *displayItem) const {
    TexturePtr thumbnail = displayItem->getThumbnailImage(&sThumbnailConfig);
    if (thumbnail && thumbnail->getState() == Texture::STATE_ERROR && mDrawables->mTextureBroken) {
        return mDrawables->mTextureBroken;
    }
    return thumbnail;
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
                TexturePtr texture = thumbnailOf(displayItem);
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
            TexturePtr texture = thumbnailOf(displayItem);
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
            TexturePtr texture = thumbnailOf(displayItem);
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
                displayList->setHovered(displayItem, mHoverSlot == index, pushDown);
            }
            if (j >= maxDisplayedItemsPerSlot) {
                continue;
            }
            TexturePtr texture = thumbnailOf(displayItem);
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
    for (FocusShadow &shadow : mFocusShadows) {
        shadow = FocusShadow();
    }

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
        // Fit the picture inside the safe area, not the whole screen, so a
        // cutout or a system bar never lands on it. The camera's aspect is the
        // window's; this is the aspect of what can actually be seen.
        const App::SafeAreaInsets &safe = App::SAFE_AREA;
        const float safeWidth = std::max(1.0f, (float)camera->mWidth - safe.left - safe.right);
        const float safeHeight = std::max(1.0f, (float)camera->mHeight - safe.top - safe.bottom);
        float viewAspect = safeWidth / safeHeight;
        // One world unit is the viewport's height, so this is the share of it
        // the picture may use.
        const float fitHeight = safeHeight / (float)camera->mHeight;
        // The insets are rarely equal at both ends, so the middle of the safe
        // area is not the middle of the screen: a cutout takes more from the
        // top than the gesture bar takes from the bottom, and the picture
        // belongs that much lower. Both axes run the way the screen does, y
        // downwards, and one world unit is the viewport's height.
        const float safeOffsetX = (safe.left - safe.right) * 0.5f / (float)camera->mHeight;
        const float safeOffsetY = (safe.top - safe.bottom) * 0.5f / (float)camera->mHeight;
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
        const TexturePtr thumbnailTexture = thumbnailOf(displayItem);
        TexturePtr texture = displayItem->getScreennailImage();
        if (isCameraZAnimating && (!texture || !texture->isLoaded())) {
            // Start the decode as the camera starts to move in, so the
            // screennail can arrive during the transition instead of after it.
            texture = thumbnailTexture;
            // The fade belongs to the centre picture. A neighbour still
            // waiting for its screennail would otherwise hold it at zero.
            if (i == 0) {
                view->prime(displayItem->getScreennailImage(), true);
                mSelectedMixRatio.setValue(0.0f);
                mSelectedMixRatio.animateValue(1.0f, 0.75f, view->getFrameTime());
            }
        }
        // Zoom uses visible tiles where the source supports cropping, otherwise a
        // higher-resolution whole image.
        const bool tiled = TiledImage::canTile(item);
        TexturePtr hiRes =
            (!tiled && zoomValue != 1.0f && i == 0 && item->getMediaType() != MediaItem::MEDIA_TYPE_VIDEO)
                ? displayItem->getHiResImage()
                : nullptr;

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
            // Load high resolution only near the focus. Tolerance scales with item width
            // because camera positions use layout pixels.
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
                if (fsTexture && fsTexture->getState() == Texture::STATE_ERROR) {
                    // A screennail that failed to decode is not coming, so the
                    // thumbnail stays at full strength. Fading it out toward
                    // the screennail would leave it dim for the fade, and
                    // restarting the fade would redraw every frame.
                    mSelectedMixRatio.setValue(1.0f);
                } else {
                    mSelectedMixRatio.setValue(0.0f);
                    mSelectedMixRatio.animateValue(1.0f, 0.75f, view->getFrameTime());
                }
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
        // The picture's own shape, taken from the item, which knew its size
        // before any of its pixels arrived. Sizing from whichever texture is to
        // hand instead means the shape changes under the picture when the
        // screennail replaces the thumbnail.
        float imageWidth = (float)texture->getWidth();
        float imageHeight = (float)texture->getHeight();
        if (item != nullptr && item->hasFullSize()) {
            imageWidth = (float)item->mFullWidth;
            imageHeight = (float)item->mFullHeight;
        } else if (fsTexture && fsTexture != thumbnailTexture && fsTexture->isLoaded()) {
            imageWidth = (float)fsTexture->getWidth();
            imageHeight = (float)fsTexture->getHeight();
        }
        bool portrait = ((theta / 90) % 2 == 1);
        if (portrait) {
            viewAspect = 1.0f / viewAspect;
        }
        // Only while it fits: zoomed in, the picture is larger than the screen
        // by intent, and the tiles drawn over it are placed from the item's own
        // centre.
        const float centerOffsetX = (zoomValue == 1.0f) ? safeOffsetX : 0.0f;
        const float centerOffsetY = (zoomValue == 1.0f) ? safeOffsetY : 0.0f;
        quad->setCenterOffset(centerOffsetX, centerOffsetY);
        quad->resizeQuad(viewAspect, u, v, imageWidth, imageHeight, fitHeight);
        const float pictureWidth = quad->getWidth();
        const float pictureHeight = quad->getHeight();

        // A thumbnail is a centre crop of the picture, so it covers only part
        // of it. Draw it at the size that part occupies rather than stretched
        // over the whole, and leave the rest empty until the screennail fills
        // it in. The thumbnail also stands in as fsTexture while the camera
        // zooms, so compare against the thumbnail itself.
        if (texture == thumbnailTexture && texture->getHeight() > 0) {
            // Hold the whole picture's area black. This pass draws additively
            // over a cleared buffer, so a zero colour adds nothing. The quad
            // still writes depth, which keeps the background out of the area.
            const float drawAlpha = view->getAlpha();
            view->setColor(0.0f, 0.0f, 0.0f, 0.0f);
            quad->bindArrays(view);
            drawDisplayItem(view, displayItem, texture, PASS_FOCUS_CONTENT, nullptr, 0.0f);
            quad->unbindArrays(view);
            view->setAlpha(drawAlpha);

            const float cropAspect = (float)texture->getWidth() / (float)texture->getHeight();
            float cropWidth = pictureWidth;
            float cropHeight = (cropAspect > 0.0f) ? (pictureWidth / cropAspect) : pictureHeight;
            if (cropHeight > pictureHeight) {
                cropHeight = pictureHeight;
                cropWidth = pictureHeight * cropAspect;
            }
            quad->setShape(cropWidth, cropHeight, u, v);
        }
        // Transparent pixels show a checkerboard rather than black. The
        // picture is blended over it, since adding would brighten it.
        const bool transparent = texture->mHasAlpha;
        if (transparent) {
            drawFocusChecker(view, displayItem, quad, centerOffsetX, centerOffsetY, u, v, alpha);
            view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        quad->bindArrays(view);
        drawDisplayItem(view, displayItem, texture, PASS_FOCUS_CONTENT, nullptr, 0.0f);
        quad->unbindArrays(view);
        if (transparent) {
            view->blendFunc(GL_ONE, GL_ONE);
        }

        // Overlay tiles only after the thumbnail-to-screennail fade settles the quad's size.
        if (i == 0 && zoomValue != 1.0f && selectedMixRatio == 1.0f && !slideshowMode) {
            drawFocusTiles(view, displayItem, quad);
        }

        if (selectedMixRatio != 0.0f && selectedMixRatio != 1.0f && fsTexture) {
            view->setAlpha(alpha * selectedMixRatio);
            // The whole picture, at the shape the item said it was.
            u = fsTexture->getNormalizedWidth();
            v = fsTexture->getNormalizedHeight();
            quad->setShape(pictureWidth, pictureHeight, u, v);
            const bool fsTransparent = fsTexture->mHasAlpha;
            if (fsTransparent) {
                view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                drawFocusChecker(view, displayItem, quad, centerOffsetX, centerOffsetY, u, v,
                                 alpha * selectedMixRatio);
            }
            quad->bindArrays(view);
            drawDisplayItem(view, displayItem, fsTexture, PASS_FOCUS_CONTENT, nullptr, 1.0f);
            quad->unbindArrays(view);
            if (fsTransparent) {
                view->blendFunc(GL_ONE, GL_ONE);
            }
        }
        if (i == 0 || slideshowMode) {
            mCurrentFocusItemWidth = pictureWidth;
            mCurrentFocusItemHeight = pictureHeight;
            if (portrait) {
                std::swap(mCurrentFocusItemWidth, mCurrentFocusItemHeight);
            }
        }
        mFocusShadows[vboIndex] = {displayItem, pictureWidth, pictureHeight, centerOffsetX, centerOffsetY, alpha};
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
    // Skip rotated images: tile rectangles use screen axes, while the quad rotates locally.
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
    // Visible screen rectangle in world units: x right, y down.
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

    // Whole-picture width in screen pixels determines tile detail.
    const float drawnWidth = (quadWidth / viewSpan) * (float)camera->mWidth;
    tiled->update(view, left, top, right, bottom, drawnWidth);

    const std::vector<TiledImage::Placed> &tiles = tiled->placedTiles();
    if (tiles.empty()) {
        return;
    }

    // Use ordinary blending so opaque tiles replace the screennail instead of adding to it.
    // Unloaded regions retain the screennail.
    view->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The same transform drawDisplayItem puts the whole picture under, so the
    // tiles land in its local space.
    const float translateZ = -displayItem->mAnimatedPosition.z;
    view->glTranslatef(-centerX, -centerY, -translateZ);
    for (const TiledImage::Placed &tile : tiles) {
        if (!tile.texture || !tile.texture->isLoaded()) {
            continue;
        }
        // Local +x points screen-left and +y screen-up; fractions count from the positive
        // corner.
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

void GridDrawManager::drawFocusChecker(RenderView *view, DisplayItem *displayItem, GridQuad *quad, float offsetX,
                                       float offsetY, float u, float v, float alpha) {
    const TexturePtr &checker = mDrawables->mTextureChecker;
    const float worldPerPixel = mCamera->worldUnitsPerPixel(displayItem->mAnimatedPosition.z);
    if (!checker || worldPerPixel <= 0.0f) {
        return;
    }
    // Cells stay one size on screen while the picture zooms, so the pattern
    // is measured from the eye's distance this frame, not from the picture.
    // It is centred on the picture and grows out from the middle. One repeat
    // of the texture is two cells.
    const float cellDp = 8.0f;
    const float repeat = 2.0f * cellDp * App::UI_DENSITY * worldPerPixel;
    const float width = quad->getWidth();
    const float height = quad->getHeight();
    const float halfWidth = width * 0.5f;
    const float halfHeight = height * 0.5f;
    const float uHalf = halfWidth / repeat;
    const float vHalf = halfHeight / repeat;
    const float previousAlpha = view->getAlpha();
    view->setAlpha(alpha);
    quad->setCorners(offsetX - halfWidth, offsetY - halfHeight, offsetX + halfWidth, offsetY + halfHeight, uHalf, vHalf,
                     -uHalf, -vHalf);
    quad->bindArrays(view);
    drawDisplayItem(view, displayItem, checker, PASS_FOCUS_CONTENT, nullptr, 0.0f);
    quad->unbindArrays(view);
    quad->setShape(width, height, u, v);
    view->setAlpha(previousAlpha);
}

void GridDrawManager::drawFocusShadows(RenderView *view, float visibility) {
    GridQuad *quad = GridDrawables::sShadowGrid;
    const TexturePtr &texture = mDrawables->mTextureShadow;
    if (quad == nullptr || !texture || visibility <= 0.0f) {
        return;
    }
    GridCamera *camera = mCamera;
    // Measured against the viewport's height in world units, so the shadow
    // keeps its proportion to the picture and grows with it as it zooms.
    const float radiusDp = 32.0f;
    const float radius = radiusDp * App::UI_DENSITY / (float)camera->mHeight;
    // Just behind the picture, so the depth test keeps the shadow off any
    // picture it reaches, its own or a neighbour sliding in.
    const float behind = 0.01f;
    const float previousAlpha = view->getAlpha();
    for (FocusShadow &shadow : mFocusShadows) {
        DisplayItem *item = shadow.item;
        if (item == nullptr || shadow.width <= 0.0f || shadow.height <= 0.0f) {
            continue;
        }
        const float halfWidth = shadow.width * 0.5f;
        const float halfHeight = shadow.height * 0.5f;
        const std::array<GridDrawables::ShadowPiece, 8> pieces =
            GridDrawables::shadowPieces(shadow.offsetX - halfWidth, shadow.offsetY - halfHeight,
                                        shadow.offsetX + halfWidth, shadow.offsetY + halfHeight, radius);

        // The transform drawDisplayItem gives the picture itself.
        const float translateX = item->mAnimatedPosition.x * camera->mOneByScale;
        const float translateY = item->mAnimatedPosition.y * camera->mOneByScale;
        const float translateZ = -item->mAnimatedPosition.z - behind;
        const float theta = item->mAnimatedImageTheta + item->mAnimatedTheta;
        view->glTranslatef(-translateX, -translateY, -translateZ);
        if (theta != 0.0f) {
            view->glRotatef(theta, 0.0f, 0.0f, 1.0f);
        }
        view->setAlpha(visibility * shadow.alpha);
        for (const GridDrawables::ShadowPiece &piece : pieces) {
            quad->setCorners(piece.xMin, piece.yMin, piece.xMax, piece.yMax, piece.uAtXMin, piece.vAtYMin,
                             piece.uAtXMax, piece.vAtYMax);
            quad->bindArrays(view);
            if (view->bind(texture)) {
                GridQuad::draw(view, 0.0f);
            }
            quad->unbindArrays(view);
        }
        if (theta != 0.0f) {
            view->glRotatef(-theta, 0.0f, 0.0f, 1.0f);
        }
        view->glTranslatef(translateX, translateY, translateZ);
        shadow = FocusShadow();
    }
    view->setAlpha(previousAlpha);
}

void GridDrawManager::drawBlendedComponents(RenderView *view, float alpha, int state, int hudMode,
                                            float stackMixRatio, float gridMixRatio,
                                            MediaBucketList &selectedBucketList, MediaBucketList &markedBucketList,
                                            bool isFeedLoading) {
    // The wall fades out as the fullscreen pictures come in, and the shadows
    // with them.
    drawFocusShadows(view, 1.0f - alpha);
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
