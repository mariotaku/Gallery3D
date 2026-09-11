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

    // Drops that cache. Being static, it would otherwise be destroyed at exit,
    // by which time the RenderView each texture asks to free its GL name is
    // long gone. The layer calls this on the way down, while the view is still
    // there to hear it.
    static void releaseStringTextures();

    // Builds the shared quads, once. They are sized from the item dimensions
    // and PIXEL_DENSITY, so releaseQuads has to come first to rebuild them at
    // a new density - see GridLayer::onDensityChanged.
    static void buildQuads(int itemWidth, int itemHeight);
    static void releaseQuads();

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
