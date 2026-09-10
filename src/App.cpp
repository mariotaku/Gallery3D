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
int SCREEN_NAIL_MAX_EDGE = 1024;
std::string ASSET_ROOT = "assets";

Drawable findDrawable(const std::string &name, bool allowHigherDensity) {
    // Two buckets. drawable-hdpi is drawn for density 1.5, exactly 1.5x
    // drawable-mdpi in every asset the port ships.
    //
    // The baseline here is the unqualified drawable folder, which Android
    // treats as mdpi, and this calls density 1. That is a shade loose: the
    // original ships both, and for a few drawables they disagree. icon_play is
    // 34 unqualified against 30 in mdpi, so calling it density 1 is out by
    // 13%. It only matters for art drawn at its own size with no hdpi variant
    // to prefer, which none of the current callers hit.
    //
    // Nothing lines up exactly anyway: PIXEL_DENSITY is the display scale times
    // CONTENT_SCALE, 2.625 on this machine, so hdpi art is still resampled by
    // 1.75. Picking the closer bucket is the point, not avoiding the resample.
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
