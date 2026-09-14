#include "platform/desktop/IccToSrgb.h"

#include <lcms2.h>

IccToSrgb::~IccToSrgb() {
    if (mTransform != nullptr) {
        cmsDeleteTransform((cmsHTRANSFORM)mTransform);
    }
}

void IccToSrgb::open(const uint8_t *profile, size_t size) {
    if (mTransform != nullptr) {
        cmsDeleteTransform((cmsHTRANSFORM)mTransform);
        mTransform = nullptr;
    }
    if (profile == nullptr || size == 0) {
        return;
    }
    cmsHPROFILE source = cmsOpenProfileFromMem(profile, (cmsUInt32Number)size);
    if (source == nullptr) {
        return;
    }
    if (cmsGetColorSpace(source) == cmsSigRgbData) {
        cmsHPROFILE srgb = cmsCreate_sRGBProfile();
        if (srgb != nullptr) {
            // The transform keeps what it needs from both profiles, so they
            // are closed straight after. Without the one pixel cache, which is
            // what lets several decode threads share one transform.
            mTransform = cmsCreateTransform(source, TYPE_RGBA_8, srgb, TYPE_RGBA_8, INTENT_PERCEPTUAL,
                                            cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);
            cmsCloseProfile(srgb);
        }
    }
    cmsCloseProfile(source);
}

void IccToSrgb::convert(uint8_t *rgba, size_t count) const {
    if (mTransform == nullptr || rgba == nullptr || count == 0) {
        return;
    }
    // Input and output share one format, which LittleCMS converts in place.
    cmsDoTransform((cmsHTRANSFORM)mTransform, rgba, rgba, (cmsUInt32Number)count);
}
