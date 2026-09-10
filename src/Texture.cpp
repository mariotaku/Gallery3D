#include "Texture.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#include "App.h"
#include "MediaItem.h"
#include "RenderView.h"

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
        // thumbnail out of the disk cache; here we decode from the file and
        // crop to the same shape so grid items fill their frame.
        int width = (int)(mConfig->thumbnailWidth * App::PIXEL_DENSITY);
        int height = (int)(mConfig->thumbnailHeight * App::PIXEL_DENSITY);
        Bitmap decoded = Bitmap::load(mItem->mFilePath, std::max(width, height) * 3);
        if (!decoded.valid()) {
            return decoded;
        }
        return decoded.coverCropped(width, height);
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

// Blends src over dst at (dstX, dstY). Both are premultiplied RGBA.
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
            float sr = (s[0] / 255.0f) * r * a;
            float sg = (s[1] / 255.0f) * g * a;
            float sb = (s[2] / 255.0f) * b * a;
            float inv = 1.0f - sa;
            d[0] = (uint8_t)std::min(255.0f, sr * 255.0f + d[0] * inv);
            d[1] = (uint8_t)std::min(255.0f, sg * 255.0f + d[1] * inv);
            d[2] = (uint8_t)std::min(255.0f, sb * 255.0f + d[2] * inv);
            d[3] = (uint8_t)std::min(255.0f, sa * 255.0f + d[3] * inv);
        }
    }
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

    TTF_Font *font = mConfig.bold ? sFontBold : sFontRegular;
    float fontSize = mConfig.fontSize;
    TTF_SetFontSize(font, fontSize);

    int textWidth = 0;
    int textHeight = 0;
    TTF_GetStringSize(font, mText.c_str(), mText.length(), &textWidth, &textHeight);

    if (mConfig.sizeMode == Config::SIZE_TEXT_TO_BOUNDS) {
        // Shrink until the string fits the fixed width, exactly as the original.
        while (textWidth >= mWidth && fontSize > 6.0f) {
            fontSize -= 1.0f;
            TTF_SetFontSize(font, fontSize);
            TTF_GetStringSize(font, mText.c_str(), mText.length(), &textWidth, &textHeight);
        }
    }

    int padding = 1 + mConfig.shadowRadius;
    int backWidth = mWidth;
    int backHeight = mHeight;
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
    // The original asked Paint for a blurred black shadow. A four way black
    // offset costs nothing and keeps labels readable over bright photos.
    if (mConfig.shadowRadius > 0) {
        int offset = std::max(1, mConfig.shadowRadius / 2);
        blendOver(result, glyphs, x - offset, y, 0.0f, 0.0f, 0.0f, 0.6f);
        blendOver(result, glyphs, x + offset, y, 0.0f, 0.0f, 0.0f, 0.6f);
        blendOver(result, glyphs, x, y - offset, 0.0f, 0.0f, 0.0f, 0.6f);
        blendOver(result, glyphs, x, y + offset, 0.0f, 0.0f, 0.0f, 0.6f);
    }
    blendOver(result, glyphs, x, y, mConfig.r, mConfig.g, mConfig.b, mConfig.a);

    if (textWidth > backWidth && mConfig.overflowMode == Config::OVERFLOW_FADE) {
        // Fade the right edge when the string overflows its box.
        int gradientLeft = backWidth - Config::FADE_WIDTH;
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
