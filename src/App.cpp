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
// Fits two stack rows in the default 1280x800 window and selects 256x64 captions.
float CONTENT_SCALE = 1.5f;
int SCREEN_NAIL_MAX_EDGE = 1024;
// No ceiling: the density decides unless a device asks for less.
int THUMBNAIL_MAX_EDGE = 0;
int HI_RES_MAX_EDGE = 2048;
int BACKDROP_BLUR = BACKDROP_BLUR_GAUSSIAN;
// Matches the standard deviation of a nine-tap box: sqrt((81 - 1) / 12).
float BACKDROP_BLUR_SIGMA = 2.58f;
SafeAreaInsets SAFE_AREA;
std::string ASSET_ROOT = "assets";

namespace {

// Ascending Android density buckets: mdpi = 1x, hdpi = 1.5x.
struct Bucket {
    const char *directory;
    float density;
};

const Bucket kBuckets[] = {
    {"drawable-mdpi", 1.0f},
    {"drawable-hdpi", 1.5f},
};

// Fallback folder; some assets differ from mdpi. Unscaled callers use its exact pixel sizes.
const char *const kFallbackDirectory = "drawable";

std::string pathIn(const char *directory, const std::string &name) {
    return assetPath(std::string(directory) + "/" + name + ".png");
}

}  // namespace

std::string assetPath(const std::string &relative) {
    if (ASSET_ROOT.empty()) {
        return relative;
    }
    return ASSET_ROOT + "/" + relative;
}

Drawable findDrawable(const std::string &name, bool allowHigherDensity) {
    if (allowHigherDensity) {
        // Choose the smallest bucket at least as dense as the chrome.
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
