#include "Texture.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include "App.h"
#include "Canvas.h"
#include "DiskCache.h"
#include "LocalDataSource.h"
#include "MediaItem.h"
#include "MediaSet.h"
#include "RenderView.h"
#include "Shared.h"

Texture::~Texture() {
    if (mId != 0 && mOwner != nullptr) {
        mOwner->queueDeleteTexture(mId);
    }
}

void Texture::clear() {
    mId = 0;
    mState = STATE_UNLOADED;
    mWidth = 0;
    mHeight = 0;
    mNormalizedWidth = 0.0f;
    mNormalizedHeight = 0.0f;
    mBitmap = Bitmap();
}

namespace {

// Gets an item's pixels, wherever they live. A source that keeps its photos
// somewhere other than this disk hands over the encoded bytes; everything else
// reads the file. Runs on a loader thread either way.
Bitmap decodeItem(MediaItem *item, int maxEdge) {
    if (item == nullptr) {
        return Bitmap();
    }
    MediaSet *set = item->mParentMediaSet;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    if (source != nullptr) {
        std::vector<uint8_t> bytes;
        if (source->readItemBytes(item, &bytes) && !bytes.empty()) {
            return Bitmap::loadFromMemory(bytes.data(), bytes.size(), maxEdge);
        }
    }
    return Bitmap::load(item->mFilePath, maxEdge);
}

// What to key the thumbnail cache on. A remote item has no path, so it falls
// back to the uri it was addressed by.
const std::string &cacheIdentity(const MediaItem *item) {
    return item->mFilePath.empty() ? item->mContentUri : item->mFilePath;
}

}  // namespace

Bitmap ResourceTexture::load(RenderView *view) {
    (void)view;
    // An unscaled texture is drawn at whatever size it loads at, and its
    // callers were written against the baseline art, so it has to stay there.
    App::Drawable drawable = App::findDrawable(mName, mScaled);
    Bitmap bitmap = Bitmap::load(drawable.path, 0);
    if (!mScaled || !bitmap.valid()) {
        return bitmap;
    }
    // What the scaled flag meant on Android: decodeResource sized the art for
    // the screen density, so a caller could draw it at its own size and get a
    // button the right size for the display. openRawResource did not, which is
    // what the _unscaled art is named for, and those callers pass false.
    //
    // findDrawable has already picked the closest density, so this is usually
    // the identity and nothing is resampled.
    float factor = App::UI_DENSITY / drawable.density;
    if (factor > 0.99f && factor < 1.01f) {
        return bitmap;
    }
    int width = (int)((float)bitmap.width() * factor + 0.5f);
    int height = (int)((float)bitmap.height() * factor + 0.5f);
    if (width <= 0 || height <= 0) {
        return bitmap;
    }
    return bitmap.scaled(width, height);
}

Bitmap FileTexture::load(RenderView *view) {
    (void)view;
    if (mItem != nullptr) {
        return decodeItem(mItem, mMaxEdge);
    }
    return Bitmap::load(mPath, mMaxEdge);
}

Bitmap MediaItemTexture::load(RenderView *view) {
    (void)view;
    if (!mItem) {
        return Bitmap();
    }
    if (mConfig) {
        // Grid thumbnail. The original pulled a pre-baked, centre cropped
        // thumbnail out of the disk cache, always 128x96, which the loader then
        // padded to 128x128. That is why GridDrawables gives the grid quad
        // texture extents of (1.0, oneByAspect): it expects the image to fill
        // the full width and exactly oneByAspect of the height of a square
        // power of two texture. The shipped grid_placeholder.png is 128x96 for
        // the same reason.
        //
        // So pick a power of two side and crop to that ratio, whatever the
        // display density. Anything else leaves the quad sampling the padding.
        int side = Shared::nextPowerOf2((int)(mConfig->thumbnailWidth * App::PIXEL_DENSITY));
        int height = side * mConfig->thumbnailHeight / mConfig->thumbnailWidth;

        // Decoding a few hundred originals costs seconds on every launch, so
        // keep the cropped result on disk. The key carries the modification
        // time and the crop size, because the size follows the display density
        // and can differ between runs.
        char suffix[64];
        SDL_snprintf(suffix, sizeof(suffix), "|%lld|%dx%d", (long long)mItem->mDateModifiedInSec,
                     side, height);
        std::string key = cacheIdentity(mItem) + suffix;
        DiskCache &cache = DiskCache::thumbnails();
        Bitmap cached = cache.get(key);
        if (cached.valid() && cached.width() == side && cached.height() == height) {
            return cached;
        }

        Bitmap decoded = decodeItem(mItem, std::max(side, height) * 2);
        if (!decoded.valid()) {
            return decoded;
        }
        Bitmap cropped = decoded.coverCropped(side, height);
        if (cropped.valid()) {
            cache.put(key, cropped);
        }
        return cropped;
    }
    // Screennail, used once an item fills the screen, so it is sized to the
    // window rather than to the original's handset era cap.
    return decodeItem(mItem, App::SCREEN_NAIL_MAX_EDGE);
}

// ---------------------------------------------------------------------------
// StringTexture
// ---------------------------------------------------------------------------


StringTexture::StringTexture(std::string text, const Config &config) : mText(std::move(text)), mConfig(config) {
    mWidth = config.width;
    mHeight = config.height;
}

int StringTexture::computeTextWidthForConfig(const std::string &text, const Config &config) {
    int width = 0;
    if (!Canvas::measureText(text, config.fontSize, config.bold, &width, nullptr)) {
        return 0;
    }
    // 10 pixel buffer to compensate for the shade at the end, as in the original.
    return (int)(10.0f * App::PIXEL_DENSITY) + width;
}

float StringTexture::computeTextWidth() const {
    int width = 0;
    Canvas::measureText(mText, mConfig.fontSize, mConfig.bold, &width, nullptr);
    return (float)width;
}

Bitmap StringTexture::load(RenderView *view) {
    (void)view;
    if (mText.empty()) {
        return Bitmap();
    }
    if (!Canvas::fontsReady()) {
        return Bitmap();
    }

    // Everything below works in device pixels: the logical box scaled by the
    // supersample factor.
    const int scale = std::max(1, mConfig.superSample);
    const int boundsWidth = mWidth * scale;
    const int boundsHeight = mHeight * scale;

    float fontSize = mConfig.fontSize * (float)scale;
    int textWidth = 0;
    int textHeight = 0;
    Canvas::measureText(mText, fontSize, mConfig.bold, &textWidth, &textHeight);

    if (mConfig.sizeMode == Config::SIZE_TEXT_TO_BOUNDS) {
        // Shrink until the string fits the fixed width, exactly as the original.
        while (textWidth >= boundsWidth && fontSize > 6.0f * (float)scale) {
            fontSize -= (float)scale;
            Canvas::measureText(mText, fontSize, mConfig.bold, &textWidth, &textHeight);
        }
    }

    int shadowRadius = mConfig.shadowRadius * scale;
    int padding = 1 + shadowRadius;
    int backWidth = boundsWidth;
    int backHeight = boundsHeight;
    if (mConfig.sizeMode == Config::SIZE_BOUNDS_TO_TEXT) {
        backWidth = textWidth + 2 * padding;
        backHeight = textHeight + padding;
    }
    if (backWidth <= 0 || backHeight <= 0) {
        return Bitmap();
    }

    Bitmap glyphs = Canvas::renderText(mText, fontSize, mConfig.bold);
    if (!glyphs.valid()) {
        return Bitmap();
    }

    int x;
    if (mConfig.xalignment == Config::ALIGN_LEFT) {
        x = padding;
    } else if (mConfig.xalignment == Config::ALIGN_RIGHT) {
        x = backWidth - padding - glyphs.width();
    } else {
        x = (backWidth - glyphs.width()) / 2;
    }
    int y;
    if (mConfig.yalignment == Config::ALIGN_TOP) {
        y = padding;
    } else if (mConfig.yalignment == Config::ALIGN_BOTTOM) {
        y = backHeight - padding - glyphs.height();
    } else {
        y = (backHeight - glyphs.height()) / 2;
    }

    Bitmap result(backWidth, backHeight);
    if (shadowRadius > 0) {
        // Soft black halo under the text so labels stay readable over a bright
        // photo, the same job Paint.setShadowLayer did.
        Bitmap shadow = Canvas::blurredCoverage(glyphs, shadowRadius);
        if (shadow.valid()) {
            Canvas::blendOver(result, shadow, x - shadowRadius, y - shadowRadius, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
    Canvas::blendOver(result, glyphs, x, y, mConfig.r, mConfig.g, mConfig.b, mConfig.a);

    if (textWidth > backWidth && mConfig.overflowMode == Config::OVERFLOW_FADE) {
        // Fade the right edge when the string overflows its box.
        int gradientLeft = backWidth - Config::FADE_WIDTH * scale;
        if (gradientLeft < 0) {
            gradientLeft = 0;
        }
        for (int py = 0; py < backHeight; ++py) {
            uint8_t *row = result.pixels() + (size_t)py * (size_t)backWidth * 4;
            for (int px = gradientLeft; px < backWidth; ++px) {
                float t = 1.0f - (float)(px - gradientLeft) / (float)(backWidth - gradientLeft);
                uint8_t *p = row + (size_t)px * 4;
                p[0] = (uint8_t)(p[0] * t);
                p[1] = (uint8_t)(p[1] * t);
                p[2] = (uint8_t)(p[2] * t);
                p[3] = (uint8_t)(p[3] * t);
            }
        }
    }
    return result;
}
