#include "App.h"

#include <cstdio>

namespace App {

namespace {

bool fileExists(const std::string &path) {
    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    fclose(file);
    return true;
}

}  // namespace

float PIXEL_DENSITY = 1.0f;
// 1.5 fills the default 1280x800 window with two rows of stacks and no
// clipping. It also crosses the 1.5 threshold that DisplaySlot and
// GridDrawables use to pick the 256x64 label texture over the 128x32 one, so
// captions stay sharp at this size.
float CONTENT_SCALE = 1.5f;
std::string ASSET_ROOT = "assets";

Drawable findDrawable(const std::string &name, bool allowHigherDensity) {
    // Only two buckets, because those are the two the original shipped that are
    // worth having here. Anything above the baseline takes the hdpi art: at the
    // default density they are the same size, so nothing is resampled at all.
    std::string hdpi = ASSET_ROOT + "/drawable-hdpi/" + name + ".png";
    if (allowHigherDensity && PIXEL_DENSITY > 1.0f && fileExists(hdpi)) {
        return Drawable{hdpi, 1.5f};
    }
    std::string baseline = ASSET_ROOT + "/drawable/" + name + ".png";
    if (fileExists(baseline)) {
        return Drawable{baseline, 1.0f};
    }
    // Not every drawable ships at every density. Take whatever there is rather
    // than hand back a path with nothing behind it.
    if (fileExists(hdpi)) {
        return Drawable{hdpi, 1.5f};
    }
    return Drawable{baseline, 1.0f};
}

std::string drawablePath(const std::string &name) {
    return findDrawable(name).path;
}

}  // namespace App
