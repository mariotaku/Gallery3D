// Text through SDL_ttf, for the platforms without a backend of their own.
//
// One face for regular and one for bold, with no fallback between faces: a
// glyph the shipped font has never seen is drawn as a box. Platforms that mind
// about that have their own backend.
#include "graphics/TextBackend.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstring>
#include <mutex>
#include <string>

#include "app/App.h"

namespace {

// SDL_ttf is not thread safe and the loader threads all draw text, so every
// entry point takes this.
std::mutex sFontMutex;
TTF_Font *sFontRegular = nullptr;
TTF_Font *sFontBold = nullptr;
bool sFontsReady = false;

// Prefer the shipped font, resolved against the asset root, then platform fonts.
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
    std::string shipped = App::assetPath(std::string("fonts/") + shippedName);
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

namespace TextBackend {

bool init() {
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

void shutdown() {
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

bool ready() {
    std::lock_guard<std::mutex> lock(sFontMutex);
    return sFontsReady;
}

bool measure(const std::string &text, float fontSize, bool bold, int *width, int *height) {
    if (fontSize <= 0.0f) {
        return false;
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

Bitmap render(const std::string &text, float fontSize, bool bold) {
    if (fontSize <= 0.0f || text.empty()) {
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

}  // namespace TextBackend
