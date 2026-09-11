#include "BackgroundLayer.h"

#include <SDL3/SDL.h>

#include "AdaptiveBackgroundTexture.h"
#include "App.h"
#include "DisplayItem.h"
#include "MediaItem.h"
#include "GridLayer.h"

// The backdrop sits at the far end of the depth buffer so everything the grid
// drew in the opaque pass stays in front of it.
static const float Z_FAR_PLANE = 0.9999f;
static const float PARALLAX = 0.5f;
static const int ADAPTIVE_BACKGROUND_WIDTH = 256;
static const int ADAPTIVE_BACKGROUND_HEIGHT = 128;
static const int MAX_ADAPTIVES_TO_KEEP_IN_MEMORY = 16;

void BackgroundLayer::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.blendedList.push_back(this);
    lists.updateList.push_back(this);
    lists.opaqueList.push_back(this);
}

bool BackgroundLayer::update(RenderView *view, float frameInterval) {
    if (!mFallbackBackground || !mFallbackBackground->isLoaded()) {
        return false;
    }
    if (!mBackground) {
        mBackground = std::make_unique<CrossFadingTexture>(mFallbackBackground);
    }
    bool retVal = mBackground->update(frameInterval);
    DisplayItem *displayItem = mGridLayer->getRepresentativeDisplayItem();
    if (displayItem) {
        mBackground->setTexture(getAdaptive(view, displayItem));
    }
    return retVal;
}

TexturePtr BackgroundLayer::getAdaptive(RenderView *view, DisplayItem *item) {
    (void)view;
    if (!item) {
        return mFallbackBackground;
    }
    // Null config: take the thumbnail the grid already asked for, never make one.
    TexturePtr itemThumbnail = item->getThumbnailImage(nullptr);
    if (!itemThumbnail || !itemThumbnail->isLoaded()) {
        return mFallbackBackground;
    }
    auto found = mCacheAdaptiveTexture.find(itemThumbnail);
    if (found != mCacheAdaptiveTexture.end()) {
        return found->second;
    }
    // Keyed by the thumbnail, built from the item. The thumbnail says the
    // wall has this photo in hand, which is when a backdrop is worth making;
    // the pixels have to come from the item, because a texture's are gone to
    // the card by then.
    TexturePtr adaptive = std::make_shared<AdaptiveBackgroundTexture>(item->mItemRef, ADAPTIVE_BACKGROUND_WIDTH,
                                                                     ADAPTIVE_BACKGROUND_HEIGHT);
    if (mCount == MAX_ADAPTIVES_TO_KEEP_IN_MEMORY) {
        mCount = 0;
        mCacheAdaptiveTexture.clear();
        SDL_Log("Clearing unused adaptive backgrounds.");
    }
    ++mCount;
    mCacheAdaptiveTexture[itemThumbnail] = adaptive;
    return adaptive;
}

void BackgroundLayer::renderOpaque(RenderView *view) {
    glClear(GL_COLOR_BUFFER_BIT);
    if (!mFallbackBackground) {
        mFallbackBackground = view->getResource(Res::drawable::default_background, false);
        view->loadTexture(mFallbackBackground);
    }
}

void BackgroundLayer::renderBlended(RenderView *view) {
    if (!mBackground || !mFallbackBackground) {
        return;
    }
    view->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    view->resetColor();
    bool bind = mBackground->bind(view);
    if (!bind) {
        if (!view->bind(mFallbackBackground)) {
            return;
        }
    } else {
        // Whatever is on screen now is the best thing to fall back to next.
        const TexturePtr &texture = mBackground->getTexture();
        if (texture && texture->isLoaded()) {
            mFallbackBackground = texture;
        }
    }

    // Stitched three times so the wrap is covered whichever way the wall scrolled.
    int cameraPosition = (int)(mGridLayer->getScrollPosition() * PARALLAX);
    int backgroundSpacing = mBackgroundBlitWidth - mBackgroundOverlap;
    if (backgroundSpacing <= 0) {
        backgroundSpacing = 1;
    }
    int anchorEdge = -cameraPosition % backgroundSpacing;
    int rightEdge = anchorEdge + backgroundSpacing;
    int leftEdge = anchorEdge - backgroundSpacing;

    // Right to left, because each tile fades out on its right edge and so has
    // to be drawn over the one it overlaps.
    view->draw2D((float)rightEdge, 0.0f, Z_FAR_PLANE, (float)mBackgroundBlitWidth, mHeight);
    view->draw2D((float)anchorEdge, 0.0f, Z_FAR_PLANE, (float)mBackgroundBlitWidth, mHeight);
    view->draw2D((float)leftEdge, 0.0f, Z_FAR_PLANE, (float)mBackgroundBlitWidth, mHeight);

    if (bind) {
        mBackground->unbind(view);
    }

    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    view->resetColor();
}

void BackgroundLayer::onSizeChanged() {
    mBackgroundBlitWidth = (int)(mWidth * 1.5f);
    mBackgroundOverlap = (int)((float)mBackgroundBlitWidth * 0.25f);
}

void BackgroundLayer::clear() {
    clearCache();
    mBackground.reset();
    mFallbackBackground.reset();
}

void BackgroundLayer::clearCache() {
    mCacheAdaptiveTexture.clear();
    mCount = 0;
}
