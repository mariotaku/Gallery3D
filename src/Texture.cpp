#include "Texture.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include "App.h"
#include "DiskCache.h"
#include "MediaItem.h"
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

Bitmap ResourceTexture::load(RenderView *view) {
    (void)view;
    return Bitmap::load(App::drawablePath(mName), 0);
}

Bitmap FileTexture::load(RenderView *view) {
    (void)view;
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
        std::string key = mItem->mFilePath + suffix;
        DiskCache &cache = DiskCache::thumbnails();
        Bitmap cached = cache.get(key);
        if (cached.valid() && cached.width() == side && cached.height() == height) {
            return cached;
        }

        Bitmap decoded = Bitmap::load(mItem->mFilePath, std::max(side, height) * 2);
        if (!decoded.valid()) {
            return decoded;
        }
        Bitmap cropped = decoded.coverCropped(side, height);
        if (cropped.valid()) {
            cache.put(key, cropped);
        }
        return cropped;
    }
    // Screennail, used once an item fills the screen.
    return Bitmap::load(mItem->mFilePath, FileTexture::MAX_RESOLUTION);
}

// ---------------------------------------------------------------------------
// StringTexture
// ---------------------------------------------------------------------------

namespace {

std::mutex sFontMutex;
TTF_Font *sFontRegular = nullptr;
TTF_Font *sFontBold = nullptr;
bool sFontsReady = false;

const char *const kFontCandidates[] = {
    "assets/fonts/Roboto-Regular.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
};

const char *const kBoldFontCandidates[] = {
    "assets/fonts/Roboto-Bold.ttf",
    "C:/Windows/Fonts/segoeuib.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
};

TTF_Font *openFirst(const char *const *candidates, size_t count, float size) {
    for (size_t i = 0; i < count; ++i) {
        TTF_Font *font = TTF_OpenFont(candidates[i], size);
        if (font) {
            return font;
        }
    }
    return nullptr;
}

// Blends src over dst at (dstX, dstY), tinting src by (r, g, b, a). src is
// straight alpha, the way SDL_ttf hands glyphs back; dst is premultiplied,
// the way the renderer wants to receive it.
void blendOver(Bitmap &dst, const Bitmap &src, int dstX, int dstY, float r, float g, float b, float a) {
    for (int y = 0; y < src.height(); ++y) {
        int ty = dstY + y;
        if (ty < 0 || ty >= dst.height()) {
            continue;
        }
        const uint8_t *srcRow = src.pixels() + (size_t)y * (size_t)src.width() * 4;
        uint8_t *dstRow = dst.pixels() + (size_t)ty * (size_t)dst.width() * 4;
        for (int x = 0; x < src.width(); ++x) {
            int tx = dstX + x;
            if (tx < 0 || tx >= dst.width()) {
                continue;
            }
            const uint8_t *s = srcRow + (size_t)x * 4;
            uint8_t *d = dstRow + (size_t)tx * 4;
            float sa = (s[3] / 255.0f) * a;
            if (sa <= 0.0f) {
                continue;
            }
            // The colour has to carry the glyph's own coverage, because the
            // result is uploaded premultiplied and the renderer blends it with
            // GL_ONE. Scaling by the tint alpha alone would push every partly
            // covered edge pixel to full brightness and throw the antialiasing
            // away.
            float sr = (s[0] / 255.0f) * r * sa;
            float sg = (s[1] / 255.0f) * g * sa;
            float sb = (s[2] / 255.0f) * b * sa;
            float inv = 1.0f - sa;
            d[0] = (uint8_t)std::min(255.0f, sr * 255.0f + d[0] * inv);
            d[1] = (uint8_t)std::min(255.0f, sg * 255.0f + d[1] * inv);
            d[2] = (uint8_t)std::min(255.0f, sb * 255.0f + d[2] * inv);
            d[3] = (uint8_t)std::min(255.0f, sa * 255.0f + d[3] * inv);
        }
    }
}

// Box blurs the coverage of a glyph bitmap and returns it as a white bitmap
// with that coverage as its alpha, padded by the radius so the halo is not
// clipped. Stands in for Paint.setShadowLayer, which drew a blurred drop
// shadow rather than offset copies. Two passes approximate a tent filter.
Bitmap blurredCoverage(const Bitmap &src, int radius) {
    if (!src.valid() || radius <= 0) {
        return Bitmap();
    }
    const int pad = radius;
    const int width = src.width() + pad * 2;
    const int height = src.height() + pad * 2;

    std::vector<float> coverage((size_t)width * (size_t)height, 0.0f);
    for (int y = 0; y < src.height(); ++y) {
        const uint8_t *row = src.pixels() + (size_t)y * (size_t)src.width() * 4;
        for (int x = 0; x < src.width(); ++x) {
            coverage[(size_t)(y + pad) * (size_t)width + (size_t)(x + pad)] = row[(size_t)x * 4 + 3] / 255.0f;
        }
    }

    std::vector<float> scratch(coverage.size(), 0.0f);
    const float norm = 1.0f / (float)(radius * 2 + 1);
    for (int pass = 0; pass < 2; ++pass) {
        // Horizontal.
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    int sx = x + k;
                    if (sx >= 0 && sx < width) {
                        sum += coverage[(size_t)y * (size_t)width + (size_t)sx];
                    }
                }
                scratch[(size_t)y * (size_t)width + (size_t)x] = sum * norm;
            }
        }
        // Vertical.
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    int sy = y + k;
                    if (sy >= 0 && sy < height) {
                        sum += scratch[(size_t)sy * (size_t)width + (size_t)x];
                    }
                }
                coverage[(size_t)y * (size_t)width + (size_t)x] = sum * norm;
            }
        }
    }

    Bitmap result(width, height);
    uint8_t *pixels = result.pixels();
    for (size_t i = 0; i < coverage.size(); ++i) {
        float value = coverage[i];
        if (value > 1.0f) {
            value = 1.0f;
        }
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = (uint8_t)(value * 255.0f + 0.5f);
    }
    return result;
}

Bitmap surfaceToBitmap(SDL_Surface *surface) {
    Bitmap result;
    if (!surface) {
        return result;
    }
    SDL_Surface *rgba = surface;
    bool owned = false;
    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        owned = true;
    }
    if (rgba) {
        result = Bitmap(rgba->w, rgba->h);
        for (int y = 0; y < rgba->h; ++y) {
            std::memcpy(result.pixels() + (size_t)y * (size_t)rgba->w * 4,
                        (const uint8_t *)rgba->pixels + (size_t)y * (size_t)rgba->pitch, (size_t)rgba->w * 4);
        }
    }
    if (owned && rgba) {
        SDL_DestroySurface(rgba);
    }
    return result;
}

}  // namespace

bool StringTexture::initFonts() {
    std::lock_guard<std::mutex> lock(sFontMutex);
    if (sFontsReady) {
        return true;
    }
    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return false;
    }
    sFontRegular = openFirst(kFontCandidates, sizeof(kFontCandidates) / sizeof(kFontCandidates[0]), 20.0f);
    sFontBold = openFirst(kBoldFontCandidates, sizeof(kBoldFontCandidates) / sizeof(kBoldFontCandidates[0]), 20.0f);
    if (!sFontRegular) {
        SDL_Log("No usable font found; text labels will be blank");
        return false;
    }
    if (!sFontBold) {
        sFontBold = sFontRegular;
    }
    sFontsReady = true;
    return true;
}

void StringTexture::shutdownFonts() {
    std::lock_guard<std::mutex> lock(sFontMutex);
    if (sFontBold && sFontBold != sFontRegular) {
        TTF_CloseFont(sFontBold);
    }
    if (sFontRegular) {
        TTF_CloseFont(sFontRegular);
    }
    sFontBold = nullptr;
    sFontRegular = nullptr;
    if (sFontsReady) {
        TTF_Quit();
    }
    sFontsReady = false;
}

StringTexture::StringTexture(std::string text, const Config &config) : mText(std::move(text)), mConfig(config) {
    mWidth = config.width;
    mHeight = config.height;
}

int StringTexture::computeTextWidthForConfig(const std::string &text, const Config &config) {
    std::lock_guard<std::mutex> lock(sFontMutex);
    if (!sFontsReady) {
        return 0;
    }
    TTF_Font *font = config.bold ? sFontBold : sFontRegular;
    TTF_SetFontSize(font, config.fontSize);
    int w = 0;
    int h = 0;
    TTF_GetStringSize(font, text.c_str(), text.length(), &w, &h);
    // 10 pixel buffer to compensate for the shade at the end, as in the original.
    return (int)(10.0f * App::PIXEL_DENSITY) + w;
}

float StringTexture::computeTextWidth() const {
    if (mText.empty()) {
        return 0.0f;
    }
    std::lock_guard<std::mutex> lock(sFontMutex);
    if (!sFontsReady) {
        return 0.0f;
    }
    TTF_Font *font = mConfig.bold ? sFontBold : sFontRegular;
    TTF_SetFontSize(font, mConfig.fontSize);
    int w = 0;
    int h = 0;
    TTF_GetStringSize(font, mText.c_str(), mText.length(), &w, &h);
    return (float)w;
}

Bitmap StringTexture::load(RenderView *view) {
    (void)view;
    if (mText.empty()) {
        return Bitmap();
    }
    std::lock_guard<std::mutex> lock(sFontMutex);
    if (!sFontsReady) {
        return Bitmap();
    }

    // Everything below works in device pixels: the logical box scaled by the
    // supersample factor.
    const int scale = std::max(1, mConfig.superSample);
    const int boundsWidth = mWidth * scale;
    const int boundsHeight = mHeight * scale;

    TTF_Font *font = mConfig.bold ? sFontBold : sFontRegular;
    float fontSize = mConfig.fontSize * (float)scale;
    TTF_SetFontSize(font, fontSize);

    int textWidth = 0;
    int textHeight = 0;
    TTF_GetStringSize(font, mText.c_str(), mText.length(), &textWidth, &textHeight);

    if (mConfig.sizeMode == Config::SIZE_TEXT_TO_BOUNDS) {
        // Shrink until the string fits the fixed width, exactly as the original.
        while (textWidth >= boundsWidth && fontSize > 6.0f * (float)scale) {
            fontSize -= (float)scale;
            TTF_SetFontSize(font, fontSize);
            TTF_GetStringSize(font, mText.c_str(), mText.length(), &textWidth, &textHeight);
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

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *rendered = TTF_RenderText_Blended(font, mText.c_str(), mText.length(), white);
    Bitmap glyphs = surfaceToBitmap(rendered);
    if (rendered) {
        SDL_DestroySurface(rendered);
    }
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
        Bitmap shadow = blurredCoverage(glyphs, shadowRadius);
        if (shadow.valid()) {
            blendOver(result, shadow, x - shadowRadius, y - shadowRadius, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
    blendOver(result, glyphs, x, y, mConfig.r, mConfig.g, mConfig.b, mConfig.a);

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
