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
float UI_DENSITY = 1.0f;
// 1.5 fills the default 1280x800 window with two rows of stacks and no
// clipping. It also crosses the 1.5 threshold that DisplaySlot and
// GridDrawables use to pick the 256x64 label texture over the 128x32 one, so
// captions stay sharp at this size.
float CONTENT_SCALE = 1.5f;
int SCREEN_NAIL_MAX_EDGE = 1024;
int HI_RES_MAX_EDGE = 2048;
std::string ASSET_ROOT = "assets";

namespace {

// The density buckets the port ships, ascending. Android's naming, and its
// numbers: mdpi is the 1x baseline and hdpi is exactly 1.5x it, which holds for
// every asset here. Add xhdpi at 2.0 above if art for it ever appears.
struct Bucket {
    const char *directory;
    float density;
};

const Bucket kBuckets[] = {
    {"drawable-mdpi", 1.0f},
    {"drawable-hdpi", 1.5f},
};

// The unqualified folder. Android treats it as mdpi, and mostly it is, but the
// original ships both and they disagree for a few assets, so it is the last
// resort rather than a bucket. Unscaled textures use it directly: their callers
// were written against these exact pixel sizes.
const char *const kFallbackDirectory = "drawable";

std::string pathIn(const char *directory, const std::string &name) {
    return ASSET_ROOT + "/" + directory + "/" + name + ".png";
}

}  // namespace

Drawable findDrawable(const std::string &name, bool allowHigherDensity) {
    if (allowHigherDensity) {
        // The smallest bucket that still has enough pixels, so art is reduced
        // rather than blown up, compared against the density the chrome is
        // actually drawn at.
        const Bucket *best = nullptr;
        for (const Bucket &bucket : kBuckets) {
            if (bucket.density + 0.001f < UI_DENSITY) {
                continue;
            }
            if (fileExists(pathIn(bucket.directory, name))) {
                best = &bucket;
                break;
            }
        }
        // Nothing dense enough exists, so take the densest that does and accept
        // the upscale.
        if (best == nullptr) {
            for (int i = (int)(sizeof(kBuckets) / sizeof(kBuckets[0])) - 1; i >= 0; --i) {
                if (fileExists(pathIn(kBuckets[i].directory, name))) {
                    best = &kBuckets[i];
                    break;
                }
            }
        }
        if (best != nullptr) {
            return Drawable{pathIn(best->directory, name), best->density};
        }
    }

    std::string fallback = pathIn(kFallbackDirectory, name);
    if (fileExists(fallback) || !allowHigherDensity) {
        return Drawable{fallback, 1.0f};
    }
    // Nothing anywhere. Hand back the fallback path so the caller reports a
    // missing file rather than a missing directory.
    return Drawable{fallback, 1.0f};
}

float drawableBucketDensity() {
    for (const Bucket &bucket : kBuckets) {
        if (bucket.density + 0.001f >= UI_DENSITY) {
            return bucket.density;
        }
    }
    return kBuckets[sizeof(kBuckets) / sizeof(kBuckets[0]) - 1].density;
}

std::string drawablePath(const std::string &name) {
    return findDrawable(name).path;
}

}  // namespace App
