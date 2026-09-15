#include "graphics/ColorProfile.h"

#include <lcms2.h>

#include <mutex>

#include "platform/desktop/IccToSrgb.h"

namespace {

// Adobe RGB (1998) as its specification gives it: a D65 white, its three
// primaries and a gamma of 563/256. Built once and shared, without the one
// pixel cache, so decode threads can use it at once.
cmsHTRANSFORM adobeRgbTransform() {
    static cmsHTRANSFORM transform = nullptr;
    static std::once_flag built;
    std::call_once(built, [] {
        const cmsCIExyY white = {0.3127, 0.3290, 1.0};
        const cmsCIExyYTRIPLE primaries = {{0.6400, 0.3300, 1.0}, {0.2100, 0.7100, 1.0}, {0.1500, 0.0600, 1.0}};
        cmsToneCurve *gamma = cmsBuildGamma(nullptr, 563.0 / 256.0);
        if (gamma == nullptr) {
            return;
        }
        cmsToneCurve *curves[3] = {gamma, gamma, gamma};
        cmsHPROFILE adobe = cmsCreateRGBProfile(&white, &primaries, curves);
        cmsHPROFILE srgb = cmsCreate_sRGBProfile();
        if (adobe != nullptr && srgb != nullptr) {
            transform = cmsCreateTransform(adobe, TYPE_RGBA_8, srgb, TYPE_RGBA_8, INTENT_PERCEPTUAL,
                                           cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);
        }
        if (adobe != nullptr) {
            cmsCloseProfile(adobe);
        }
        if (srgb != nullptr) {
            cmsCloseProfile(srgb);
        }
        cmsFreeToneCurve(gamma);
    });
    return transform;
}

}  // namespace

bool ColorProfile::iccToSrgb(const std::vector<uint8_t> &profile, uint8_t *rgba, size_t count) {
    if (profile.empty() || rgba == nullptr || count == 0) {
        return false;
    }
    IccToSrgb toSrgb;
    toSrgb.open(profile.data(), profile.size());
    if (!toSrgb) {
        return false;
    }
    toSrgb.convert(rgba, count);
    return true;
}

bool ColorProfile::adobeRgbToSrgb(uint8_t *rgba, size_t count) {
    const cmsHTRANSFORM transform = adobeRgbTransform();
    if (transform == nullptr || rgba == nullptr || count == 0) {
        return false;
    }
    cmsDoTransform(transform, rgba, rgba, (cmsUInt32Number)count);
    return true;
}
