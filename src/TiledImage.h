// Tiled image rendering after davemorrissey/subsampling-scale-image-view.
// Fetch visible regions at power-of-two sample levels over a fallback screennail.
// Cropping requires DataSource::supportsRegions; SDL_image/stb cannot decode regions.
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "Texture.h"

class MediaItem;
class RenderView;

class TiledImage {
  public:
    // One tile as the draw path wants it: a texture, and where it belongs on
    // the picture. The fractions run from the picture's left and top edges.
    struct Placed {
        TexturePtr texture;
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    explicit TiledImage(MediaItem *item) : mItem(item) {}

    // Requires source cropping support and known original dimensions.
    static bool canTile(const MediaItem *item);

    // Tile grid at one sample level; region dimensions must stay inside the image.
    struct Grid {
        int sampleSize = 1;  // how many of the original's pixels go into one
        int regionEdge = 0;  // how much of the original one tile covers
        int columns = 0;
        int rows = 0;
    };

    // One tile's rectangle in the original's pixels, trimmed at the far edges.
    struct Region {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int outWidth = 0;
        int outHeight = 0;
    };

    // Grid for a whole-picture width of drawnWidth screen pixels; fetch only visible detail.
    static Grid gridFor(int fullWidth, int fullHeight, float drawnWidth);

    static Region regionFor(const Grid &grid, int fullWidth, int fullHeight, int column, int row);

    // Request missing visible tiles and drop unwanted ones. View bounds are image fractions;
    // drawnWidth is the whole picture's width in device pixels and selects sample detail.
    void update(RenderView *view, float left, float top, float right, float bottom, float drawnWidth);

    // The tiles to draw, in load order. Only ones with pixels appear.
    const std::vector<Placed> &placedTiles() const {
        return mPlaced;
    }

    // How many of the tiles wanted right now have arrived, and how many are
    // wanted. Equal means the picture is showing at full detail.
    int loadedCount() const {
        return mLoadedCount;
    }
    int wantedCount() const {
        return mWantedCount;
    }

    void clear();

    // Maximum decoded tile edge. A multiple of the IIIF server's 256-pixel pyramid tiles
    // aligns requests with cached regions.
    static const int kTileEdge = 512;

  private:
    struct Tile {
        TexturePtr texture;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int sampleSize = 1;
        uint64_t lastWanted = 0;
    };

    // Tile cache capacity; evict least-recently-wanted tiles first.
    static const int kMaxTiles = 64;

    // Maximum new requests per pass, limiting stale queued work after zoom changes.
    static const int kMaxStartsPerUpdate = 8;

    static int64_t keyFor(int sampleSize, int column, int row);

    MediaItem *mItem;
    std::map<int64_t, Tile> mTiles;
    std::vector<Placed> mPlaced;
    uint64_t mPass = 0;
    int mSampleSize = 0;
    int mLoadedCount = 0;
    int mWantedCount = 0;
};
