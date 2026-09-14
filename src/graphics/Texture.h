// Port of com.cooliris.media.Texture and its subclasses.
#pragma once

#include <memory>
#include <string>

#include "graphics/Bitmap.h"
#include "graphics/ImageDecode.h"
#include "graphics/gles2.h"

class RenderView;
class MediaItem;

class Texture;
using TexturePtr = std::shared_ptr<Texture>;

// Decode source bytes or a local file with maxEdge downscaling (0 = unlimited),
// or take the source's own thumbnail when it keeps one that large.
// May answer inline or later; pixel consumers use this instead of Texture::load.
void decodeItemPixels(MediaItem *item, int maxEdge, ImageDecode::Callback done);

// The texture edge a grid thumbnail is decoded and uploaded at: a power of two,
// from the density, held under maxEdge when that is not zero. A mip chain needs
// the power of two; the cap is for a device that cannot upload what its own
// density asks for without the wall stopping.
int thumbnailTextureEdge(int thumbnailWidth, float density, int maxEdge);

class Texture {
  public:
    enum State {
        STATE_UNLOADED = 0,
        STATE_QUEUED = 1,
        STATE_LOADING = 2,
        STATE_LOADED = 3,
        STATE_ERROR = 4,
    };

    virtual ~Texture();

    virtual bool isCached() const {
        return false;
    }

    virtual bool isUncachedVideo() const {
        return false;
    }

    // When this returns false the texture is skipped instead of being queued.
    virtual bool shouldQueue() const {
        return true;
    }

    // Starts loading and finishes through RenderView::finishLoad, inline or later.
    // self keeps the texture alive. The default invokes synchronous load().
    virtual void startLoad(RenderView *view, const TexturePtr &self);

    // Build mipmaps for grid thumbnails that minify during zoom and tilt.
    virtual bool wantsMipmaps() const {
        return false;
    }

    // Wrap and sample texel by texel, for a pattern tiled across a quad. The
    // bitmap has to be a power of two already, since padding would tile too.
    virtual bool wantsRepeat() const {
        return false;
    }

    void clear();

    bool isLoaded() const {
        return mState == STATE_LOADED;
    }

    int getState() const {
        return mState;
    }

    int getWidth() const {
        return mWidth;
    }

    int getHeight() const {
        return mHeight;
    }

    float getNormalizedWidth() const {
        return mNormalizedWidth;
    }

    float getNormalizedHeight() const {
        return mNormalizedHeight;
    }

    // Runs on a loader thread. Returns an invalid bitmap on failure.
    virtual Bitmap load(RenderView *view) = 0;

    // Mirrors the package private fields of the original, which the render view
    // and the draw manager both poke at directly.
    int mState = STATE_UNLOADED;
    GLuint mId = 0;
    int mWidth = 0;
    int mHeight = 0;
    float mNormalizedWidth = 0.0f;
    float mNormalizedHeight = 0.0f;
    // Some pixel is less than fully opaque. Found when the bitmap arrives.
    bool mHasAlpha = false;
    Bitmap mBitmap;
    RenderView *mOwner = nullptr;
    // Bookkeeping for the texture budget in RenderView: how much GPU memory
    // this one holds, and the frame it was last bound on.
    size_t mBytes = 0;
    // When it was last drawn, in milliseconds. Not a frame number: a frame is
    // a different length of time on a 60Hz panel and a 120Hz one, so counting
    // them would throw work away twice as fast on the faster screen.
    uint64_t mLastUsedMs = 0;
};

// A bitmap built in code rather than decoded.
class GeneratedTexture : public Texture {
  public:
    GeneratedTexture(Bitmap bitmap, bool repeat) : mSource(std::move(bitmap)), mRepeat(repeat) {}

    bool isCached() const override {
        return true;
    }

    bool wantsRepeat() const override {
        return mRepeat;
    }

    Bitmap load(RenderView *view) override {
        (void)view;
        return mSource;
    }

  private:
    Bitmap mSource;
    bool mRepeat;
};

// Loads a PNG out of assets/drawable.
class ResourceTexture : public Texture {
  public:
    ResourceTexture(std::string name, bool scaled) : mName(std::move(name)), mScaled(scaled) {}

    bool isCached() const override {
        return true;
    }

    Bitmap load(RenderView *view) override;

  private:
    std::string mName;
    bool mScaled;
};

// Loads a photo off disk, downscaled so neither edge exceeds maxEdge.
// Replaces UriTexture.
class FileTexture : public Texture {
  public:
    static const int MAX_RESOLUTION = 1024;

    // Pass the item when there is one, so a source that does not keep its
    // photos on this disk is asked for the bytes rather than the path.
    explicit FileTexture(std::string path, int maxEdge = MAX_RESOLUTION, MediaItem *item = nullptr)
        : mPath(std::move(path)), mMaxEdge(maxEdge), mItem(item) {}

    Bitmap load(RenderView *view) override;
    void startLoad(RenderView *view, const TexturePtr &self) override;

  private:
    std::string mPath;
    int mMaxEdge;
    MediaItem *mItem;
};

// Encoded region of the original, downscaled by its source to a tile.
// Requires DataSource::supportsRegions; coordinates use original pixels.
class RegionTexture : public Texture {
  public:
    RegionTexture(MediaItem *item, int x, int y, int width, int height, int outWidth, int outHeight)
        : mItem(item), mX(x), mY(y), mWidth(width), mHeight(height), mOutWidth(outWidth), mOutHeight(outHeight) {}

    void startLoad(RenderView *view, const TexturePtr &self) override;

    // Tiles draw near one texel per pixel and do not need mipmaps.
    bool wantsMipmaps() const override {
        return false;
    }

    Bitmap load(RenderView *view) override;

  private:
    MediaItem *mItem;
    int mX;
    int mY;
    int mWidth;
    int mHeight;
    int mOutWidth;
    int mOutHeight;
};

// The grid thumbnail for one media item. Replaces MediaItemTexture.
class MediaItemTexture : public Texture {
  public:
    struct Config {
        int thumbnailWidth = 128;
        int thumbnailHeight = 96;
    };

    MediaItemTexture(const Config *config, MediaItem *item) : mConfig(config), mItem(item) {}

    void startLoad(RenderView *view, const TexturePtr &self) override;

    // Only the grid thumbnails. The fullscreen path draws close to one to one.
    bool wantsMipmaps() const override {
        return mConfig != nullptr;
    }

    bool isCached() const override {
        return mConfig != nullptr;
    }

    Bitmap load(RenderView *view) override;

  private:
    const Config *mConfig;
    MediaItem *mItem;
};

// Renders a string into a texture. Replaces StringTexture, which drew through
// android.graphics.Canvas; this one draws through SDL_ttf.
class StringTexture : public Texture {
  public:
    struct Config {
        static const int SIZE_EXACT = 0;
        static const int SIZE_TEXT_TO_BOUNDS = 1;
        static const int SIZE_BOUNDS_TO_TEXT = 2;

        static const int OVERFLOW_CLIP = 0;
        static const int OVERFLOW_ELLIPSIZE = 1;
        static const int OVERFLOW_FADE = 2;

        static const int ALIGN_HCENTER = 0;
        static const int ALIGN_LEFT = 1;
        static const int ALIGN_RIGHT = 2;
        static const int ALIGN_TOP = 3;
        static const int ALIGN_BOTTOM = 4;
        static const int ALIGN_VCENTER = 5;

        static const int FADE_WIDTH = 30;

        float fontSize = 20.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        int shadowRadius = 4;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strikeThrough = false;
        int width = 256;
        int height = 32;
        int xalignment = ALIGN_LEFT;
        int yalignment = ALIGN_VCENTER;
        int sizeMode = SIZE_BOUNDS_TO_TEXT;
        int overflowMode = OVERFLOW_FADE;
        // Supersample the logical label box for sharper 3D glyphs.
        // Use 1 for draw2D textures blitted at native pixel size.
        int superSample = 1;
    };

    StringTexture(std::string text, const Config &config);

    bool isCached() const override {
        return true;
    }

    float computeTextWidth() const;

    Bitmap load(RenderView *view) override;

    static int computeTextWidthForConfig(const std::string &text, const Config &config);

  private:
    std::string mText;
    Config mConfig;
};
