#include "graphics/Texture.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include "app/App.h"
#include "graphics/Canvas.h"
#include "core/DiskCache.h"
#include "media/LocalDataSource.h"
#include "graphics/DrawableLoad.h"
#include "graphics/ImageDecode.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "graphics/RenderView.h"
#include "core/Shared.h"

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
    mHasAlpha = false;
    mBitmap = Bitmap();
}

namespace {

// Decode source bytes, falling back to a local path. Source and decoder
// callbacks may complete inline or later.
void decodeItem(MediaItem *item, int maxEdge, ImageDecode::Callback done) {
    if (item == nullptr) {
        done(Bitmap());
        return;
    }
    MediaSet *set = item->mParentMediaSet;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    const std::string path = item->mFilePath;

    auto decodeBytes = [maxEdge, done](std::vector<uint8_t> bytes) {
        ImageDecode::decode(std::move(bytes), maxEdge, done);
    };

    if (source == nullptr) {
        std::vector<uint8_t> bytes;
        if (!Bitmap::readFile(path, &bytes)) {
            done(Bitmap());
            return;
        }
        decodeBytes(std::move(bytes));
        return;
    }

    source->requestItemBytes(item, [path, decodeBytes, done](bool ok, std::vector<uint8_t> bytes) {
        if (ok && !bytes.empty()) {
            decodeBytes(std::move(bytes));
            return;
        }
        // Local sources return false to request a file-path read.
        std::vector<uint8_t> fromFile;
        if (!Bitmap::readFile(path, &fromFile)) {
            done(Bitmap());
            return;
        }
        decodeBytes(std::move(fromFile));
    });
}

// The source's own thumbnail when it keeps one that reaches maxEdge, and a
// decode of the item's bytes otherwise.
void decodeThumbnail(MediaItem *item, int maxEdge, ImageDecode::Callback done) {
    MediaSet *set = (item != nullptr) ? item->mParentMediaSet : nullptr;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    Bitmap thumbnail;
    if (source != nullptr && source->readThumbnail(item, maxEdge, &thumbnail)) {
        done(std::move(thumbnail));
        return;
    }
    decodeItem(item, maxEdge, std::move(done));
}

// Cache by local path, falling back to the remote content URI.
const std::string &cacheIdentity(const MediaItem *item) {
    return item->mFilePath.empty() ? item->mContentUri : item->mFilePath;
}

}  // namespace

void decodeItemPixels(MediaItem *item, int maxEdge, ImageDecode::Callback done) {
    decodeThumbnail(item, maxEdge, std::move(done));
}

void Texture::startLoad(RenderView *view, const TexturePtr &self) {
    view->finishLoad(self, load(view));
}

Bitmap ResourceTexture::load(RenderView *view) {
    (void)view;
    // An unscaled texture is drawn at whatever size it loads at, and its
    // callers were written against the baseline art, so it has to stay there.
    DrawableLoad::Result drawable = DrawableLoad::load(mName, mScaled);
    Bitmap bitmap = std::move(drawable.bitmap);
    if (!mScaled || !bitmap.valid()) {
        return bitmap;
    }
    // Scale selected density-bucket art to display density. Unscaled callers retain baseline
    // pixels.
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
    // Only reached where there is no item behind it, which means a plain path
    // and a decoder that answers at once.
    return Bitmap::load(mPath, mMaxEdge);
}

void FileTexture::startLoad(RenderView *view, const TexturePtr &self) {
    if (mItem == nullptr) {
        view->finishLoad(self, Bitmap::load(mPath, mMaxEdge));
        return;
    }
    decodeItem(mItem, mMaxEdge, [view, self](Bitmap bitmap) { view->finishLoad(self, std::move(bitmap)); });
}

Bitmap RegionTexture::load(RenderView *view) {
    // Never used: startLoad does the work, because the source may answer later.
    (void)view;
    return Bitmap();
}

void RegionTexture::startLoad(RenderView *view, const TexturePtr &self) {
    MediaSet *set = (mItem != nullptr) ? mItem->mParentMediaSet : nullptr;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    if (source == nullptr || !source->supportsRegions(mItem)) {
        view->finishLoad(self, Bitmap());
        return;
    }
    source->requestRegion(mItem, mX, mY, mWidth, mHeight, mSampleSize,
                          [view, self](Bitmap bitmap) { view->finishLoad(self, std::move(bitmap)); });
}

int thumbnailTextureEdge(int thumbnailWidth, float density, int maxEdge) {
    if (thumbnailWidth <= 0 || density <= 0.0f) {
        return 0;
    }
    // The nearer power of two, not the next one up. At a density of 4.5 a 128
    // unit thumbnail wants 576 pixels, and rounding up to 1024 would hold three
    // times the pixels of a 512 that is already wider than the grid draws it.
    const int wanted = Shared::nearestPowerOf2((int)(thumbnailWidth * density));
    // A device that cannot upload that much without the wall stopping asks for
    // less through wall.thumbnail-max.
    if (maxEdge > 0 && wanted > maxEdge) {
        return maxEdge;
    }
    return wanted;
}

Bitmap MediaItemTexture::load(RenderView *view) {
    // startLoad handles asynchronous decoding; load is required by the base class.
    (void)view;
    return Bitmap();
}

void MediaItemTexture::startLoad(RenderView *view, const TexturePtr &self) {
    if (!mItem) {
        view->finishLoad(self, Bitmap());
        return;
    }
    // Before the pixels, so what the source finds out, such as the rotation,
    // is on the item by the time they are drawn.
    DataSource *source = (mItem->mParentMediaSet != nullptr) ? mItem->mParentMediaSet->mDataSource : nullptr;
    auto prepare = [this, source](DataSource::ItemLoad load) {
        if (source != nullptr) {
            source->prepareItem(mItem, load);
        }
    };
    if (!mConfig) {
        prepare(DataSource::ItemLoad::Whole);
        // Size fullscreen screennails to the window.
        decodeItem(mItem, App::SCREEN_NAIL_MAX_EDGE,
                   [view, self](Bitmap bitmap) { view->finishLoad(self, std::move(bitmap)); });
        return;
    }

    // Grid extents are (1.0, oneByAspect): centre-crop to that ratio with a
    // power-of-two width so the quad does not sample texture padding.
    //
    const int side = thumbnailTextureEdge(mConfig->thumbnailWidth, App::PIXEL_DENSITY, App::THUMBNAIL_MAX_EDGE);
    const int height = side * mConfig->thumbnailHeight / mConfig->thumbnailWidth;

    // Cache cropped thumbnails by modification time and density-dependent crop size.
    char suffix[64];
    SDL_snprintf(suffix, sizeof(suffix), "|%lld|%dx%d", (long long)mItem->mDateModifiedInSec, side, height);
    const std::string key = cacheIdentity(mItem) + suffix;
    DiskCache &cache = DiskCache::thumbnails();
    Bitmap cached = cache.get(key);
    // Accept cached images up to the requested size; small originals are never enlarged.
    if (cached.valid() && cached.width() <= side &&
        cached.height() == cached.width() * mConfig->thumbnailHeight / mConfig->thumbnailWidth) {
        prepare(DataSource::ItemLoad::CachedThumbnail);
        view->finishLoad(self, std::move(cached));
        return;
    }
    prepare(DataSource::ItemLoad::Thumbnail);

    // Crop and cache after decoding; capture continuation inputs by value for asynchronous
    // completion.
    const int thumbnailWidth = mConfig->thumbnailWidth;
    const int thumbnailHeight = mConfig->thumbnailHeight;
    decodeThumbnail(mItem, std::max(side, height) * 2,
                    [view, self, side, height, key, thumbnailWidth, thumbnailHeight](Bitmap decoded) {
        if (!decoded.valid()) {
            view->finishLoad(self, std::move(decoded));
            return;
        }

        // Do not enlarge beyond source resolution. Smaller power-of-two textures
        // preserve the quad's (1.0, oneByAspect) extents.
        int fittedSide = side;
        int fittedHeight = height;
        while (fittedSide > thumbnailWidth &&
               (fittedSide > decoded.width() || fittedHeight > decoded.height())) {
            fittedSide /= 2;
            fittedHeight = fittedSide * thumbnailHeight / thumbnailWidth;
        }

        Bitmap cropped = decoded.coverCropped(fittedSide, fittedHeight);
        if (cropped.valid()) {
            DiskCache::thumbnails().put(key, cropped);
        }
        view->finishLoad(self, std::move(cropped));
    });
}

// StringTexture


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

    std::string text = mText;
    float fontSize = mConfig.fontSize * (float)scale;
    int textWidth = 0;
    int textHeight = 0;
    Canvas::measureText(text, fontSize, mConfig.bold, &textWidth, &textHeight);

    int shadowRadius = mConfig.shadowRadius * scale;
    int padding = 1 + shadowRadius;

    if (mConfig.sizeMode == Config::SIZE_TEXT_TO_BOUNDS) {
        // Shrink until the string fits the fixed width, exactly as the original.
        while (textWidth >= boundsWidth && fontSize > 6.0f * (float)scale) {
            fontSize -= (float)scale;
            Canvas::measureText(text, fontSize, mConfig.bold, &textWidth, &textHeight);
        }
    } else if (mConfig.overflowMode == Config::OVERFLOW_ELLIPSIZE) {
        // Cut the tail off instead, which holds the font size steady across
        // labels of different lengths.
        const std::string ellipsis = "...";
        const int room = boundsWidth - 2 * padding;
        if (textWidth > room) {
            int ellipsisWidth = 0;
            Canvas::measureText(ellipsis, fontSize, mConfig.bold, &ellipsisWidth, nullptr);
            size_t fit = Canvas::lengthToFit(text, fontSize, mConfig.bold, room - ellipsisWidth);
            text = text.substr(0, fit) + ellipsis;
            Canvas::measureText(text, fontSize, mConfig.bold, &textWidth, &textHeight);
        }
    }

    int backWidth = boundsWidth;
    int backHeight = boundsHeight;
    if (mConfig.sizeMode == Config::SIZE_BOUNDS_TO_TEXT) {
        backWidth = textWidth + 2 * padding;
        backHeight = textHeight + padding;
    }
    if (backWidth <= 0 || backHeight <= 0) {
        return Bitmap();
    }

    Bitmap glyphs = Canvas::renderText(text, fontSize, mConfig.bold);
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
            const int pad = Canvas::blurPadding(shadowRadius);
            Canvas::blendOver(result, shadow, x - pad, y - pad, 0.0f, 0.0f, 0.0f, 1.0f);
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
