#include "GridDrawables.h"

#include <algorithm>
#include <cmath>

#include "App.h"
#include "GridLayer.h"
#include "LocalDataSource.h"
#include "MediaSet.h"
#include "RenderView.h"
#include "Shared.h"

namespace {

// A label box about this many wall cells wide. The stacks in the album view sit
// a cell apart, so a label may run wider than the tile under it without
// reaching its neighbour.
const float kLabelWidthInCells = 1.5f;

// Tall enough for one line plus the shadow around it.
const float kLabelHeightInFonts = 1.6f;

}  // namespace

// Chrome-sized text that happens to be drawn in the scene, so it follows
// display scale like the breadcrumb rather than the wall's PIXEL_DENSITY. The
// cell scaling cancels out between the box and the quad, leaving this value as
// the glyph height in device pixels, so 16 here is 16dp on any screen and at
// any wall.scale.
float GridDrawables::labelFontSize() {
    return 16.0f * App::UI_DENSITY;
}

// Both sides are powers of two. sTextGrid samples the whole texture, and
// RenderView pads a non-power-of-two bitmap out to one, which would leave the
// quad drawing the padding alongside the label.
int GridDrawables::labelTextureWidth() {
    return Shared::nearestPowerOf2((int)(kLabelWidthInCells * (float)GridLayer::itemWidthForDensity() + 0.5f));
}

int GridDrawables::labelTextureHeight() {
    // Rounded up, never down: a box shorter than the line clips the glyphs.
    return Shared::nextPowerOf2((int)(kLabelHeightInFonts * labelFontSize() + 0.5f));
}

Bitmap GridDrawables::shadowBitmap(int size, float maxAlpha) {
    Bitmap bitmap(size, size);
    if (!bitmap.valid()) {
        return bitmap;
    }
    // A Gaussian cut off at the rim and lowered so it reaches zero there.
    const float tail = std::exp(-4.0f);
    auto falloff = [tail](float distance) {
        distance = std::clamp(distance, 0.0f, 1.0f);
        return (std::exp(-4.0f * distance * distance) - tail) / (1.0f - tail);
    };
    const float half = (float)size * 0.5f;
    uint8_t *pixels = bitmap.pixels();
    for (int y = 0; y < size; ++y) {
        const float dy = std::fabs((float)y + 0.5f - half) / half;
        for (int x = 0; x < size; ++x) {
            const float dx = std::fabs((float)x + 0.5f - half) / half;
            const float alpha = maxAlpha * falloff(dx) * falloff(dy);
            uint8_t *pixel = pixels + ((size_t)y * (size_t)size + (size_t)x) * 4;
            pixel[0] = 0;
            pixel[1] = 0;
            pixel[2] = 0;
            pixel[3] = (uint8_t)std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        }
    }
    return bitmap;
}

std::array<GridDrawables::ShadowPiece, 8> GridDrawables::shadowPieces(float left, float bottom, float right,
                                                                      float top, float radius) {
    // The texture's middle, 0.5, sits on the picture's edge and its rim, 0 or
    // 1, on the shadow's outer edge. Along an edge the texture does not change,
    // so the edge pieces stretch the middle column or row.
    const float outerLeft = left - radius;
    const float outerRight = right + radius;
    const float outerBottom = bottom - radius;
    const float outerTop = top + radius;
    return {{
        {outerLeft, outerBottom, left, bottom, 0.0f, 0.0f, 0.5f, 0.5f},
        {left, outerBottom, right, bottom, 0.5f, 0.0f, 0.5f, 0.5f},
        {right, outerBottom, outerRight, bottom, 0.5f, 0.0f, 1.0f, 0.5f},
        {outerLeft, bottom, left, top, 0.0f, 0.5f, 0.5f, 0.5f},
        {right, bottom, outerRight, top, 0.5f, 0.5f, 1.0f, 0.5f},
        {outerLeft, top, left, outerTop, 0.0f, 0.5f, 0.5f, 1.0f},
        {left, top, right, outerTop, 0.5f, 0.5f, 0.5f, 1.0f},
        {right, top, outerRight, outerTop, 0.5f, 0.5f, 1.0f, 1.0f},
    }};
}

Bitmap GridDrawables::checkerBitmap() {
    Bitmap bitmap(2, 2);
    const uint8_t light = 0xcc;
    const uint8_t dark = 0x99;
    uint8_t *pixels = bitmap.pixels();
    for (int i = 0; i < 4; ++i) {
        const uint8_t grey = (i == 0 || i == 3) ? light : dark;
        pixels[i * 4] = grey;
        pixels[i * 4 + 1] = grey;
        pixels[i * 4 + 2] = grey;
        pixels[i * 4 + 3] = 255;
    }
    return bitmap;
}

GridQuad *GridDrawables::sGrid = nullptr;
GridQuadFrame *GridDrawables::sFrame = nullptr;
GridQuad *GridDrawables::sTextGrid = nullptr;
GridQuad *GridDrawables::sSelectedGrid = nullptr;
GridQuad *GridDrawables::sVideoGrid = nullptr;
GridQuad *GridDrawables::sLocationGrid = nullptr;
GridQuad *GridDrawables::sSourceIconGrid = nullptr;
GridQuad *GridDrawables::sFullscreenGrid[3] = {nullptr, nullptr, nullptr};
GridQuad *GridDrawables::sTileGrid = nullptr;
GridQuad *GridDrawables::sShadowGrid = nullptr;
std::map<std::string, std::shared_ptr<StringTexture>> GridDrawables::sStringTextureTable;

void GridDrawables::releaseStringTextures() {
    sStringTextureTable.clear();
}

GridDrawables::GridDrawables(int itemWidth, int itemHeight) {
    buildQuads(itemWidth, itemHeight);
}

void GridDrawables::buildQuads(int itemWidth, int itemHeight) {
    if (sGrid != nullptr) {
        return;
    }
    const float height = 1.0f;
    const float width = (float)(height * itemWidth) / (float)itemHeight;
    const float aspectRatio = (float)itemWidth / (float)itemHeight;
    const float oneByAspect = 1.0f / aspectRatio;

    sGrid = GridQuad::createGridQuad(width, height, 0, 0, 1.0f, oneByAspect, true);

    // The quads used in fullscreen. Their vertices move as the image changes,
    // so they live in dynamic buffers.
    for (int i = 0; i < 3; ++i) {
        sFullscreenGrid[i] = GridQuad::createGridQuad(width, height, 0, 0, 1.0f, oneByAspect, false);
        sFullscreenGrid[i]->setDynamic(true);
    }
    sTileGrid = GridQuad::createGridQuad(width, height, 0, 0, 1.0f, oneByAspect, false);
    sTileGrid->setDynamic(true);
    sShadowGrid = GridQuad::createGridQuad(width, height, 0, 0, 1.0f, 1.0f, false);
    sShadowGrid->setDynamic(true);

    // Supplementary quads for the checkmarks, video overlay and location button.
    float sizeOfSelectedIcon = 32.0f * App::PIXEL_DENSITY / (float)itemHeight;
    float sizeOfLocationIcon = 52.0f * App::PIXEL_DENSITY / (float)itemHeight;
    float sizeOfSourceIcon = 76.0f * App::PIXEL_DENSITY / (float)itemHeight;
    sSelectedGrid = GridQuad::createGridQuad(sizeOfSelectedIcon, sizeOfSelectedIcon, -0.5f, 0.25f, 1.0f, 1.0f, false);
    sVideoGrid = GridQuad::createGridQuad(sizeOfSelectedIcon, sizeOfSelectedIcon, -0.08f, -0.09f, 1.0f, 1.0f, false);
    sLocationGrid = GridQuad::createGridQuad(sizeOfLocationIcon, sizeOfLocationIcon, 0, 0, 1.0f, 1.0f, false);
    sSourceIconGrid = GridQuad::createGridQuad(sizeOfSourceIcon, sizeOfSourceIcon, 0, 0, 1.0f, 1.0f, false);

    // The quad for the text label. One texture pixel maps to one cell pixel, so
    // the glyphs arrive at the size the font was rendered at.
    float textWidth = ((float)labelTextureWidth() / (float)itemWidth) * width;
    float textHeight = ((float)labelTextureHeight() / (float)itemHeight) * height;
    // Hung from its top edge rather than centred. The label is drawn at the top
    // of its box, so a box grown to clear a taller font would otherwise carry
    // the text up with it.
    sTextGrid = GridQuad::createGridQuad(textWidth, textHeight, 0, -textHeight * 0.5f, 1.0f, 1.0f, false);

    sFrame = GridQuadFrame::createFrame(width, height, itemWidth, itemHeight);
}

void GridDrawables::releaseQuads() {
    // Release density-dependent quads before rebuilding. Their types lack destructors,
    // so GL buffers must be released explicitly.
    GridQuad *quads[] = {sGrid,         sFullscreenGrid[0], sFullscreenGrid[1], sFullscreenGrid[2], sSelectedGrid,
                         sVideoGrid,    sLocationGrid,      sSourceIconGrid,    sTextGrid,          sTileGrid,
                         sShadowGrid};
    for (GridQuad *quad : quads) {
        if (quad != nullptr) {
            quad->freeHardwareBuffers();
            delete quad;
        }
    }
    sGrid = nullptr;
    for (int i = 0; i < 3; ++i) {
        sFullscreenGrid[i] = nullptr;
    }
    sTileGrid = nullptr;
    sShadowGrid = nullptr;
    sSelectedGrid = nullptr;
    sVideoGrid = nullptr;
    sLocationGrid = nullptr;
    sSourceIconGrid = nullptr;
    sTextGrid = nullptr;

    if (sFrame != nullptr) {
        sFrame->freeHardwareBuffers();
        delete sFrame;
        sFrame = nullptr;
    }
}

void GridDrawables::onSurfaceCreated(RenderView *view) {
    sGrid->freeHardwareBuffers();
    sGrid->generateHardwareBuffers();

    for (int i = 0; i < 3; ++i) {
        sFullscreenGrid[i]->freeHardwareBuffers();
        sFullscreenGrid[i]->generateHardwareBuffers();
    }

    sTileGrid->freeHardwareBuffers();
    sTileGrid->generateHardwareBuffers();

    sShadowGrid->freeHardwareBuffers();
    sShadowGrid->generateHardwareBuffers();

    sSelectedGrid->freeHardwareBuffers();
    sVideoGrid->freeHardwareBuffers();
    sLocationGrid->freeHardwareBuffers();
    sSourceIconGrid->freeHardwareBuffers();
    sSelectedGrid->generateHardwareBuffers();
    sVideoGrid->generateHardwareBuffers();
    sLocationGrid->generateHardwareBuffers();
    sSourceIconGrid->generateHardwareBuffers();

    sTextGrid->freeHardwareBuffers();
    sTextGrid->generateHardwareBuffers();

    sFrame->freeHardwareBuffers();
    sFrame->generateHardwareBuffers();

    sStringTextureTable.clear();

    mTextureFrame = view->getResource(Res::drawable::stack_frame, false);
    mTextureGridFrame = view->getResource(Res::drawable::grid_frame, false);
    mTextureFrameFocus = view->getResource(Res::drawable::stack_frame_focus, false);
    mTextureFramePressed = view->getResource(Res::drawable::stack_frame_gold, false);
    mTextureLocation = view->getResource(Res::drawable::btn_location_filter_unscaled, false);
    mTextureVideo = view->getResource(Res::drawable::videooverlay, false);
    mTextureCheckmarkOn = view->getResource(Res::drawable::grid_check_on, false);
    mTextureCheckmarkOff = view->getResource(Res::drawable::grid_check_off, false);
    mTextureCameraSmall = view->getResource(Res::drawable::icon_camera_small_unscaled, false);
    mTexturePicasaSmall = view->getResource(Res::drawable::icon_picasa_small_unscaled, false);
    mTextureTransparent = view->getResource(Res::drawable::transparent, false);
    mTexturePlaceholder = view->getResource(Res::drawable::grid_placeholder, false);

    view->loadTexture(mTextureFrame);
    view->loadTexture(mTextureGridFrame);
    view->loadTexture(mTextureFrameFocus);
    view->loadTexture(mTextureFramePressed);

    // Light enough that the shadow lifts the picture off the backdrop without
    // drawing a line around it.
    mTextureShadow = std::make_shared<GeneratedTexture>(shadowBitmap(64, 0.28f), false);
    mTextureChecker = std::make_shared<GeneratedTexture>(checkerBitmap(), true);
    view->loadTexture(mTextureShadow);
    view->loadTexture(mTextureChecker);
}

const char *GridDrawables::getIconForSet(MediaSet *set, bool scaled) {
    // The scaled version is for HUD rendering, the unscaled one for 3D.
    if (scaled) {
        if (set == nullptr) {
            return Res::drawable::icon_folder_small;
        }
        if (set->mPicasaAlbumId != Shared::INVALID) {
            return Res::drawable::icon_picasa_small;
        }
        if (set->mIsCameraRoll) {
            return Res::drawable::icon_camera_small;
        }
        return Res::drawable::icon_folder_small;
    }
    if (set == nullptr) {
        return Res::drawable::icon_folder_small_unscaled;
    }
    if (set->mPicasaAlbumId != Shared::INVALID) {
        return Res::drawable::icon_picasa_small_unscaled;
    }
    if (set->mIsCameraRoll) {
        return Res::drawable::icon_camera_small_unscaled;
    }
    return Res::drawable::icon_folder_small_unscaled;
}
