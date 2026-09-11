// Port of com.cooliris.media.Texture and its subclasses.
#pragma once

#include <memory>
#include <string>

#include "Bitmap.h"
#include "gles2.h"

class RenderView;
class MediaItem;

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

    // Whether to build a mip chain. Worth it only for something drawn much
    // smaller than it is stored, which on this wall means the grid thumbnails:
    // zoomed out they minify hard, and one bilinear tap out of a full size
    // image crawls as the camera moves.
    virtual bool wantsMipmaps() const {
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
    Bitmap mBitmap;
    RenderView *mOwner = nullptr;
    // Bookkeeping for the texture budget in RenderView: how much GPU memory
    // this one holds, and the frame it was last bound on.
    size_t mBytes = 0;
    uint64_t mLastUsedFrame = 0;
};

using TexturePtr = std::shared_ptr<Texture>;

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

  private:
    std::string mPath;
    int mMaxEdge;
    MediaItem *mItem;
};

// The grid thumbnail for one media item. Replaces MediaItemTexture.
class MediaItemTexture : public Texture {
  public:
    struct Config {
        int thumbnailWidth = 128;
        int thumbnailHeight = 96;
    };

    MediaItemTexture(const Config *config, MediaItem *item) : mConfig(config), mItem(item) {}

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
        // Renders the texture this many times larger than the logical box. The
        // 3D label quad maps the whole texture whatever its resolution, so this
        // buys crisper glyphs for free. Leave at 1 for anything draw2D blits at
        // its own pixel size.
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
