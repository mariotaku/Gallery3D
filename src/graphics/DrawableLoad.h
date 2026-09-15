// Loads the app's drawables. Desktop and iOS read the PNGs under
// assets/drawable*, and App::findDrawable chooses the density bucket. Android
// asks its own resources: the build copies the same PNGs into res/drawable-*dpi,
// so Android chooses the bucket and scales the pixels to the display's density.
#pragma once

#include <string>

#include "graphics/Bitmap.h"

namespace DrawableLoad {

// Looks up the platform's classes. Call once on the thread that runs main(),
// before any drawable loads.
void init();

struct Result {
    Bitmap bitmap;
    // The display density the pixels are drawn for. Scale by
    // UI_DENSITY / density to reach the size the chrome draws at.
    float density = 1.0f;
};

// Pass scaled false for the plain drawable folder's art, at its own pixel size.
Result load(const std::string &name, bool scaled = true);

// A nine-patch named with its .9, such as popup.9.
struct NinePatchSource {
    Bitmap bitmap;
    float density = 1.0f;
    // True when the bitmap still carries the one pixel guide border and the
    // stretch region has to be read from it. False when the platform read the
    // region already, and the bitmap is the art alone.
    bool hasGuides = true;
    // Half open, in the art's coordinates. Set only when hasGuides is false.
    int stretchX0 = 0;
    int stretchX1 = 0;
    int stretchY0 = 0;
    int stretchY1 = 0;
};

NinePatchSource loadNinePatch(const std::string &name);

}  // namespace DrawableLoad
