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
#include "graphics/SystemFont.h"

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

// Asks the platform for its interface font, and falls back to the face the app
// ships. No paths are written out here: which file holds a family, and what a
// machine has installed, is what fontconfig and CoreText are for.
TTF_Font *openFirst(const char *shippedName, float size, bool bold, std::string *openedPath) {
    const std::string system = SystemFont::path(bold);
    if (!system.empty()) {
        if (TTF_Font *font = TTF_OpenFont(system.c_str(), size)) {
            *openedPath = system;
            return font;
        }
        SDL_Log("The system font at %s did not open, falling back", system.c_str());
    }
    std::string shipped = App::assetPath(std::string("fonts/") + shippedName);
    if (TTF_Font *font = TTF_OpenFont(shipped.c_str(), size)) {
        *openedPath = shipped;
        return font;
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
    std::string regularPath;
    sFontRegular = openFirst(kShippedRegular, 20.0f, false, &regularPath);
    std::string boldPath;
    sFontBold = openFirst(kShippedBold, 20.0f, true, &boldPath);
    if (sFontBold != nullptr && !boldPath.empty() && boldPath == regularPath) {
        // The platform named one file for both weights, which a font collection
        // is. TTF_OpenFont reads its first face either way, so the bold is the
        // regular until it is thickened here.
        TTF_SetFontStyle(sFontBold, TTF_STYLE_BOLD);
    }
    if (!sFontRegular) {
        SDL_Log("No usable font found; text will be blank");
        return false;
    }
    if (!sFontBold) {
        // A system with one weight of its interface font, which is what a TV
        // ships. Open that face a second time and let SDL_ttf thicken it: both
        // names pointing at one font would draw every label bold, because the
        // style belongs to the font rather than to the call that draws with it.
        sFontBold = TTF_OpenFont(regularPath.c_str(), 20.0f);
        if (sFontBold != nullptr) {
            TTF_SetFontStyle(sFontBold, TTF_STYLE_BOLD);
        } else {
            sFontBold = sFontRegular;
        }
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
