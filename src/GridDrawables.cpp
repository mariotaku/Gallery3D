#include "GridDrawables.h"

#include "App.h"
#include "LocalDataSource.h"
#include "MediaSet.h"
#include "RenderView.h"
#include "Shared.h"

GridQuad *GridDrawables::sGrid = nullptr;
GridQuadFrame *GridDrawables::sFrame = nullptr;
GridQuad *GridDrawables::sTextGrid = nullptr;
GridQuad *GridDrawables::sSelectedGrid = nullptr;
GridQuad *GridDrawables::sVideoGrid = nullptr;
GridQuad *GridDrawables::sLocationGrid = nullptr;
GridQuad *GridDrawables::sSourceIconGrid = nullptr;
GridQuad *GridDrawables::sFullscreenGrid[3] = {nullptr, nullptr, nullptr};
std::map<std::string, std::shared_ptr<StringTexture>> GridDrawables::sStringTextureTable;

void GridDrawables::releaseStringTextures() {
    sStringTextureTable.clear();
}

GridDrawables::GridDrawables(int itemWidth, int itemHeight) {
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

    // Supplementary quads for the checkmarks, video overlay and location button.
    float sizeOfSelectedIcon = 32.0f * App::PIXEL_DENSITY / (float)itemHeight;
    float sizeOfLocationIcon = 52.0f * App::PIXEL_DENSITY / (float)itemHeight;
    float sizeOfSourceIcon = 76.0f * App::PIXEL_DENSITY / (float)itemHeight;
    sSelectedGrid = GridQuad::createGridQuad(sizeOfSelectedIcon, sizeOfSelectedIcon, -0.5f, 0.25f, 1.0f, 1.0f, false);
    sVideoGrid = GridQuad::createGridQuad(sizeOfSelectedIcon, sizeOfSelectedIcon, -0.08f, -0.09f, 1.0f, 1.0f, false);
    sLocationGrid = GridQuad::createGridQuad(sizeOfLocationIcon, sizeOfLocationIcon, 0, 0, 1.0f, 1.0f, false);
    sSourceIconGrid = GridQuad::createGridQuad(sizeOfSourceIcon, sizeOfSourceIcon, 0, 0, 1.0f, 1.0f, false);

    // The quad for the text label.
    float seedTextWidth = (App::PIXEL_DENSITY < 1.5f) ? 128.0f : 256.0f;
    float textWidth = (seedTextWidth / (float)itemWidth) * width;
    float textHeightPow2 = (App::PIXEL_DENSITY < 1.5f) ? 32.0f : 64.0f;
    float textHeight = (textHeightPow2 / (float)itemHeight) * height;
    sTextGrid = GridQuad::createGridQuad(textWidth, textHeight, 0, 0.0f, 1.0f, 1.0f, false);

    sFrame = GridQuadFrame::createFrame(width, height, itemWidth, itemHeight);
}

void GridDrawables::onSurfaceCreated(RenderView *view) {
    sGrid->freeHardwareBuffers();
    sGrid->generateHardwareBuffers();

    for (int i = 0; i < 3; ++i) {
        sFullscreenGrid[i]->freeHardwareBuffers();
        sFullscreenGrid[i]->generateHardwareBuffers();
    }

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
        if (set->mId == LocalDataSource::CAMERA_BUCKET_ID) {
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
    if (set->mId == LocalDataSource::CAMERA_BUCKET_ID) {
        return Res::drawable::icon_camera_small_unscaled;
    }
    return Res::drawable::icon_folder_small_unscaled;
}
