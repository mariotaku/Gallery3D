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
#include "ImageDecode.h"
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

// An item's pixels, wherever they live and whenever they arrive. A source that
// keeps its photos somewhere other than this disk hands over the bytes;
// everything else reads the file. Then the platform's decoder turns them into
// pixels. Both halves may answer before this returns, which is what happens
// natively, or much later from the browser.
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
        // The source had nothing, so fall back to the path. A local source
        // answers false here by design: its photos are files, and this is the
        // read.
        std::vector<uint8_t> fromFile;
        if (!Bitmap::readFile(path, &fromFile)) {
            done(Bitmap());
            return;
        }
        decodeBytes(std::move(fromFile));
    });
}

// What to key the thumbnail cache on. A remote item has no path, so it falls
// back to the uri it was addressed by.
// Whether this item's bytes come off the network. The same walk decodeItem
// does: the set an item belongs to knows which source made it.
bool itemLoadsOverNetwork(const MediaItem *item) {
    if (item == nullptr) {
        return false;
    }
    MediaSet *set = item->mParentMediaSet;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    return source != nullptr && source->readsBlockOnNetwork();
}

const std::string &cacheIdentity(const MediaItem *item) {
    return item->mFilePath.empty() ? item->mContentUri : item->mFilePath;
}

}  // namespace

void decodeItemPixels(MediaItem *item, int maxEdge, ImageDecode::Callback done) {
    decodeItem(item, maxEdge, std::move(done));
}

void Texture::startLoad(RenderView *view, const TexturePtr &self) {
    view->finishLoad(self, load(view));
}

bool FileTexture::loadsOverNetwork() const {
    return itemLoadsOverNetwork(mItem);
}

bool MediaItemTexture::loadsOverNetwork() const {
    return itemLoadsOverNetwork(mItem);
}

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

bool RegionTexture::loadsOverNetwork() const {
    return itemLoadsOverNetwork(mItem);
}

Bitmap RegionTexture::load(RenderView *view) {
    // Never used: startLoad does the work, because the source may answer later.
    (void)view;
    return Bitmap();
}

void RegionTexture::startLoad(RenderView *view, const TexturePtr &self) {
    MediaSet *set = (mItem != nullptr) ? mItem->mParentMediaSet : nullptr;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    if (source == nullptr || !source->supportsRegions()) {
        view->finishLoad(self, Bitmap());
        return;
    }
    source->requestRegionBytes(mItem, mX, mY, mWidth, mHeight, mOutWidth, mOutHeight,
                               [view, self](bool ok, std::vector<uint8_t> bytes) {
                                   if (!ok || bytes.empty()) {
                                       view->finishLoad(self, Bitmap());
                                       return;
                                   }
                                   // No size limit here. The source was asked
                                   // for a tile sized piece and that is what
                                   // came back, so capping it again would only
                                   // throw away pixels that were paid for.
                                   ImageDecode::decode(std::move(bytes), 0, [view, self](Bitmap bitmap) {
                                       view->finishLoad(self, std::move(bitmap));
                                   });
                               });
}

Bitmap MediaItemTexture::load(RenderView *view) {
    // Never used: startLoad below does the work, because a decode may not
    // answer on this thread. Here because the base class still declares it.
    (void)view;
    return Bitmap();
}

void MediaItemTexture::startLoad(RenderView *view, const TexturePtr &self) {
    if (!mItem) {
        view->finishLoad(self, Bitmap());
        return;
    }
    if (!mConfig) {
        // Screennail, used once an item fills the screen, so it is sized to the
        // window rather than to the original's handset era cap.
        decodeItem(mItem, App::SCREEN_NAIL_MAX_EDGE,
                   [view, self](Bitmap bitmap) { view->finishLoad(self, std::move(bitmap)); });
        return;
    }

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
    const int side = Shared::nextPowerOf2((int)(mConfig->thumbnailWidth * App::PIXEL_DENSITY));
    const int height = side * mConfig->thumbnailHeight / mConfig->thumbnailWidth;

    // Decoding a few hundred originals costs seconds on every launch, so
    // keep the cropped result on disk. The key carries the modification
    // time and the crop size, because the size follows the display density
    // and can differ between runs.
    char suffix[64];
    SDL_snprintf(suffix, sizeof(suffix), "|%lld|%dx%d", (long long)mItem->mDateModifiedInSec, side, height);
    const std::string key = cacheIdentity(mItem) + suffix;
    DiskCache &cache = DiskCache::thumbnails();
    Bitmap cached = cache.get(key);
    // At most the size asked for, and possibly smaller: what is stored was
    // fitted to the picture rather than enlarged to the request. The key
    // already carries the requested size, so anything under it was fitted from
    // this same image and is the right thing to reuse. Testing for equality
    // here missed every one of those and re-fetched forever.
    if (cached.valid() && cached.width() <= side &&
        cached.height() == cached.width() * mConfig->thumbnailHeight / mConfig->thumbnailWidth) {
        view->finishLoad(self, std::move(cached));
        return;
    }

    // The cropping and the caching happen after the decode now, wherever that
    // finishes. Everything the continuation needs is copied into it, since the
    // texture may outlive this call by a long way.
    const int thumbnailWidth = mConfig->thumbnailWidth;
    const int thumbnailHeight = mConfig->thumbnailHeight;
    decodeItem(mItem, std::max(side, height) * 2,
               [view, self, side, height, key, thumbnailWidth, thumbnailHeight](Bitmap decoded) {
        if (!decoded.valid()) {
            view->finishLoad(self, std::move(decoded));
            return;
        }

        // Never larger than the picture actually is.
        //
        // The size above follows the display density, and a dense phone asks
        // for 1024x768 where a desktop asks for 512x384. A remote source hands
        // over 843 pixels, so the larger of those is an enlargement: four times
        // the texture memory for the same detail, slightly softer. Forty eight
        // covers at three megabytes apiece also sit right on the texture budget
        // and keep evicting each other.
        //
        // The quad's extents are (1.0, oneByAspect), which is a ratio rather
        // than a resolution, so a smaller power of two is free to use.
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
