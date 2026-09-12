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
    // Shared quad repositioned for each fullscreen tile. Excluded from GridLayer's
    // per-frame update, which would overwrite the tile corners.
    static GridQuad *sTileGrid;

    // Text labels cached by string.
    static std::map<std::string, std::shared_ptr<StringTexture>> sStringTextureTable;

    // The album label's bitmap, in pixels. buildQuads sizes sTextGrid from the
    // same numbers, so the two never disagree and glyph size on screen follows
    // the font size alone. Both are multiples of the wall cell, which keeps a
    // label the same share of a tile at every density.
    static float labelFontSize();
    static int labelTextureWidth();
    static int labelTextureHeight();

    // Clear textures before RenderView is destroyed so they can release their GL names.
    static void releaseStringTextures();

    // Build shared quads from item dimensions and PIXEL_DENSITY. Call releaseQuads before
    // rebuilding.
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
