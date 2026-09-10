// Port of com.cooliris.media.GridDrawables.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "GridQuad.h"
#include "GridQuadFrame.h"
#include "Texture.h"

class RenderView;
class MediaSet;

class GridDrawables {
  public:
    // The display primitives, shared by every grid layer as in the original.
    static GridQuad *sGrid;
    static GridQuadFrame *sFrame;
    static GridQuad *sTextGrid;
    static GridQuad *sSelectedGrid;
    static GridQuad *sVideoGrid;
    static GridQuad *sLocationGrid;
    static GridQuad *sSourceIconGrid;
    static GridQuad *sFullscreenGrid[3];

    // Text labels are cached by string, exactly as before.
    static std::map<std::string, std::shared_ptr<StringTexture>> sStringTextureTable;

    TexturePtr mTextureFrame;
    TexturePtr mTextureGridFrame;
    TexturePtr mTextureFrameFocus;
    TexturePtr mTextureFramePressed;
    TexturePtr mTextureLocation;
    TexturePtr mTextureVideo;
    TexturePtr mTextureCheckmarkOn;
    TexturePtr mTextureCheckmarkOff;
    TexturePtr mTextureCameraSmall;
    TexturePtr mTexturePicasaSmall;
    TexturePtr mTextureTransparent;
    TexturePtr mTexturePlaceholder;

    GridDrawables(int itemWidth, int itemHeight);

    void onSurfaceCreated(RenderView *view);

    const char *getIconForSet(MediaSet *set, bool scaled);
};
