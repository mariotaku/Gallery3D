// One picture drawn as a grid of pieces, after
// davemorrissey/subsampling-scale-image-view.
//
// A picture in one texture has two ceilings. The texture has to fit what the
// driver allows, commonly 4096 or 8192 on a phone, and the museum's originals
// pass both - a Seurat here is 9310 pixels across. And every pixel is paid for,
// including the nine tenths off screen once the view is zoomed in.
//
// A grid has neither. Only the pieces on screen are fetched, and the grid is
// rebuilt whenever the zoom crosses a power of two, which keeps a tile close to
// one texel per pixel at any zoom: tiles are the same size on screen
// throughout, and it is the piece of the original behind each one that grows
// and shrinks.
//
// The screennail stays underneath as the bottom layer, so there is never a
// hole. A tile that has not arrived shows the blurry version of itself rather
// than nothing, which is the difference between a picture sharpening and a
// picture flashing.
//
// Nothing here decodes. Cutting a rectangle out of a jpeg is not something
// SDL_image offers - the api takes a whole file and gives a whole surface, and
// the web build's stb backend has no more than that - so the cropping belongs
// to whoever holds the original. For the museum that is the IIIF server, where
// the wanted rectangle is part of the url. See DataSource::supportsRegions.
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

    // Whether this item can be drawn this way at all: its source has to be able
    // to crop, and its original's size has to be known before anything is
    // fetched, because the grid is laid out over pixels nothing has seen yet.
    static bool canTile(const MediaItem *item);

    // The grid a picture is cut into at one level. Public and free of any
    // state so the arithmetic can be checked on its own: the sizes it produces
    // are what the museum's server is asked for, and a rectangle that runs one
    // pixel off the edge comes back as an error rather than a trimmed tile.
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

    // The grid to use when the whole picture is being drawn `drawnWidth` pixels
    // wide. Coarser as the picture gets smaller on screen, which is the
    // subsampling: never fetching detail finer than the screen can show.
    static Grid gridFor(int fullWidth, int fullHeight, float drawnWidth);

    static Region regionFor(const Grid &grid, int fullWidth, int fullHeight, int column, int row);

    // Works out which tiles the given view of the picture needs, starts the
    // ones that are missing, and drops what is no longer wanted.
    //
    // The rectangle is the part of the picture on screen, as fractions of it.
    // `drawnWidth` is how wide the whole picture is being drawn, in device
    // pixels, which is what decides how much of the original a tile covers.
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

    // A tile is this many pixels on its longest edge once scaled down. Large
    // enough that a screen is a dozen or so tiles rather than hundreds, and
    // small enough that one arriving is a visible step rather than a wait.
    //
    // It is also a multiple of 256, which is the tile size the museum's IIIF
    // server has already cut its own pyramid into, so a tile lands on that grid
    // and is served from a cache rather than resampled per request.
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

    // How many tiles to keep. A screen holds at most a couple of dozen, and the
    // rest are what panning back over lands on again. Past this the ones wanted
    // longest ago go.
    static const int kMaxTiles = 64;

    // How many to start in one pass. A zoom out changes every tile at once, and
    // queueing the lot would put a hundred requests in front of the ones that
    // are actually on screen now.
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
