#include "Canvas.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "App.h"

namespace {

// SDL_ttf is not thread safe and the loader threads all draw text, so every
// entry point below takes this.
std::mutex sFontMutex;
TTF_Font *sFontRegular = nullptr;
TTF_Font *sFontBold = nullptr;
bool sFontsReady = false;

// A shipped font is preferred, then whatever the platform is likely to have.
// The shipped name is resolved against the asset root rather than the working
// directory, because the app is not necessarily launched from beside its
// binary.
const char *const kShippedRegular = "Roboto-Regular.ttf";
const char *const kShippedBold = "Roboto-Bold.ttf";

const char *const kFontCandidates[] = {
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/Library/Fonts/Arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
};

const char *const kBoldFontCandidates[] = {
    "C:/Windows/Fonts/segoeuib.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
};

TTF_Font *openFirst(const char *shippedName, const char *const *candidates, size_t count, float size) {
    std::string shipped = App::ASSET_ROOT + "/fonts/" + shippedName;
    if (TTF_Font *font = TTF_OpenFont(shipped.c_str(), size)) {
        return font;
    }
    for (size_t i = 0; i < count; ++i) {
        TTF_Font *font = TTF_OpenFont(candidates[i], size);
        if (font) {
            return font;
        }
    }
    return nullptr;
}

// Call with sFontMutex held.
TTF_Font *fontFor(float fontSize, bool bold) {
    if (!sFontsReady) {
        return nullptr;
    }
    TTF_Font *font = bold ? sFontBold : sFontRegular;
    TTF_SetFontSize(font, fontSize);
    return font;
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

namespace Canvas {

bool initFonts() {
    std::lock_guard<std::mutex> lock(sFontMutex);
    if (sFontsReady) {
        return true;
    }
    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return false;
    }
    sFontRegular =
        openFirst(kShippedRegular, kFontCandidates, sizeof(kFontCandidates) / sizeof(kFontCandidates[0]), 20.0f);
    sFontBold = openFirst(kShippedBold, kBoldFontCandidates,
                          sizeof(kBoldFontCandidates) / sizeof(kBoldFontCandidates[0]), 20.0f);
    if (!sFontRegular) {
        SDL_Log("No usable font found; text will be blank");
        return false;
    }
    if (!sFontBold) {
        sFontBold = sFontRegular;
    }
    sFontsReady = true;
    return true;
}

void shutdownFonts() {
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

bool fontsReady() {
    std::lock_guard<std::mutex> lock(sFontMutex);
    return sFontsReady;
}

bool measureText(const std::string &text, float fontSize, bool bold, int *width, int *height) {
    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
    std::lock_guard<std::mutex> lock(sFontMutex);
    TTF_Font *font = fontFor(fontSize, bold);
    if (!font) {
        return false;
    }
    int w = 0;
    int h = 0;
    TTF_GetStringSize(font, text.c_str(), text.length(), &w, &h);
    if (width) {
        *width = w;
    }
    if (height) {
        *height = h;
    }
    return true;
}

size_t lengthToFit(const std::string &text, float fontSize, bool bold, int maxWidth) {
    if (maxWidth <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(sFontMutex);
    TTF_Font *font = fontFor(fontSize, bold);
    if (!font) {
        return 0;
    }
    int w = 0;
    int h = 0;
    TTF_GetStringSize(font, text.c_str(), text.length(), &w, &h);
    if (w <= maxWidth) {
        return text.length();
    }
    // Walk back until it fits. Step over whole UTF-8 sequences so a multibyte
    // character is never cut in half.
    size_t length = text.length();
    while (length > 0) {
        do {
            --length;
        } while (length > 0 && (text[length] & 0xC0) == 0x80);
        TTF_GetStringSize(font, text.c_str(), length, &w, &h);
        if (w <= maxWidth) {
            break;
        }
    }
    return length;
}

Bitmap renderText(const std::string &text, float fontSize, bool bold) {
    if (text.empty()) {
        return Bitmap();
    }
    std::lock_guard<std::mutex> lock(sFontMutex);
    TTF_Font *font = fontFor(fontSize, bold);
    if (!font) {
        return Bitmap();
    }
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *rendered = TTF_RenderText_Blended(font, text.c_str(), text.length(), white);
    Bitmap glyphs = surfaceToBitmap(rendered);
    if (rendered) {
        SDL_DestroySurface(rendered);
    }
    return glyphs;
}

void blendOver(Bitmap &dst, const Bitmap &src, int dstX, int dstY, float r, float g, float b, float a) {
    if (!dst.valid() || !src.valid()) {
        return;
    }
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
            // The colour carries the source's own coverage, because the result
            // is premultiplied and the renderer blends it with GL_ONE. Scaling
            // by the tint alpha alone would push every partly covered edge
            // pixel to full brightness and throw the antialiasing away.
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

void blit(Bitmap &dst, const Bitmap &src, int dstX, int dstY, float alpha) {
    if (!dst.valid() || !src.valid() || alpha <= 0.0f) {
        return;
    }
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
            // Source is already premultiplied, so scaling every channel by
            // alpha keeps it that way.
            float sa = (s[3] / 255.0f) * alpha;
            if (sa <= 0.0f) {
                continue;
            }
            float inv = 1.0f - sa;
            for (int c = 0; c < 4; ++c) {
                d[c] = (uint8_t)std::min(255.0f, s[c] * alpha + d[c] * inv);
            }
        }
    }
}

void blitScaled(Bitmap &dst, const Bitmap &src, int dstX, int dstY, int width, int height, float alpha) {
    if (!dst.valid() || !src.valid() || width <= 0 || height <= 0 || alpha <= 0.0f) {
        return;
    }
    // Nearest sampling is enough: the callers stretch a one pixel column or a
    // fixed cap, never a photo.
    for (int y = 0; y < height; ++y) {
        int ty = dstY + y;
        if (ty < 0 || ty >= dst.height()) {
            continue;
        }
        int sy = (int)((int64_t)y * src.height() / height);
        sy = std::min(sy, src.height() - 1);
        const uint8_t *srcRow = src.pixels() + (size_t)sy * (size_t)src.width() * 4;
        uint8_t *dstRow = dst.pixels() + (size_t)ty * (size_t)dst.width() * 4;
        for (int x = 0; x < width; ++x) {
            int tx = dstX + x;
            if (tx < 0 || tx >= dst.width()) {
                continue;
            }
            int sx = (int)((int64_t)x * src.width() / width);
            sx = std::min(sx, src.width() - 1);
            const uint8_t *s = srcRow + (size_t)sx * 4;
            uint8_t *d = dstRow + (size_t)tx * 4;
            float sa = (s[3] / 255.0f) * alpha;
            if (sa <= 0.0f) {
                continue;
            }
            float inv = 1.0f - sa;
            for (int c = 0; c < 4; ++c) {
                d[c] = (uint8_t)std::min(255.0f, s[c] * alpha + d[c] * inv);
            }
        }
    }
}

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
    // Two box passes approximate a tent filter, which is close enough to the
    // blur the original asked Paint for.
    for (int pass = 0; pass < 2; ++pass) {
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
        float value = std::min(1.0f, coverage[i]);
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = (uint8_t)(value * 255.0f + 0.5f);
    }
    return result;
}

void drawText(Bitmap &dst, const std::string &text, int x, int y, float fontSize, bool bold, float r, float g,
              float b, float a, int shadowRadius) {
    Bitmap glyphs = renderText(text, fontSize, bold);
    if (!glyphs.valid()) {
        return;
    }
    if (shadowRadius > 0) {
        Bitmap shadow = blurredCoverage(glyphs, shadowRadius);
        if (shadow.valid()) {
            blendOver(dst, shadow, x - shadowRadius, y - shadowRadius, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
    blendOver(dst, glyphs, x, y, r, g, b, a);
}

void fillRect(Bitmap &dst, int x, int y, int width, int height, float r, float g, float b, float a) {
    if (!dst.valid() || a <= 0.0f) {
        return;
    }
    float inv = 1.0f - a;
    for (int py = y; py < y + height; ++py) {
        if (py < 0 || py >= dst.height()) {
            continue;
        }
        uint8_t *row = dst.pixels() + (size_t)py * (size_t)dst.width() * 4;
        for (int px = x; px < x + width; ++px) {
            if (px < 0 || px >= dst.width()) {
                continue;
            }
            uint8_t *d = row + (size_t)px * 4;
            d[0] = (uint8_t)std::min(255.0f, r * a * 255.0f + d[0] * inv);
            d[1] = (uint8_t)std::min(255.0f, g * a * 255.0f + d[1] * inv);
            d[2] = (uint8_t)std::min(255.0f, b * a * 255.0f + d[2] * inv);
            d[3] = (uint8_t)std::min(255.0f, a * 255.0f + d[3] * inv);
        }
    }
}

}  // namespace Canvas
