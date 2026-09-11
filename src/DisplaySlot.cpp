#include "DisplaySlot.h"

#include "App.h"
#include "MediaSet.h"
#include "Shared.h"

namespace {

StringTexture::Config sCaptionStyle;
StringTexture::Config sClusterStyle;
StringTexture::Config sLocationStyle;
bool sStylesReady = false;

}  // namespace

void DisplaySlot::initStyles() {
    if (sStylesReady) {
        return;
    }
    int labelWidth = (App::PIXEL_DENSITY < 1.5f) ? 128 : 256;
    int labelHeight = (App::PIXEL_DENSITY < 1.5f) ? 32 : 64;

    sCaptionStyle.sizeMode = StringTexture::Config::SIZE_TEXT_TO_BOUNDS;
    sCaptionStyle.fontSize = 16 * App::PIXEL_DENSITY;
    sCaptionStyle.bold = true;
    sCaptionStyle.width = labelWidth;
    sCaptionStyle.height = labelHeight;
    sCaptionStyle.yalignment = StringTexture::Config::ALIGN_TOP;
    sCaptionStyle.xalignment = StringTexture::Config::ALIGN_HCENTER;
    sCaptionStyle.superSample = 2;

    sClusterStyle = sCaptionStyle;

    sLocationStyle.sizeMode = StringTexture::Config::SIZE_TEXT_TO_BOUNDS;
    sLocationStyle.fontSize = 12 * App::PIXEL_DENSITY;
    sLocationStyle.width = labelWidth;
    sLocationStyle.height = labelHeight;
    sLocationStyle.xalignment = StringTexture::Config::ALIGN_HCENTER;
    sLocationStyle.superSample = 2;

    sStylesReady = true;
}

void DisplaySlot::setMediaSet(MediaSet *set) {
    mSetRef = set;
    mTitle.clear();
    mTitleImage.reset();
    mLocationImage.reset();
    if (set && set->mReverseGeocodedLocation.empty()) {
        set->mReverseGeocodedLocationRequestMade = false;
        set->mReverseGeocodedLocationComputed = false;
    }
}

bool DisplaySlot::hasValidLocation() const {
    return mSetRef != nullptr && !mSetRef->mReverseGeocodedLocation.empty();
}

std::shared_ptr<StringTexture> DisplaySlot::getTextureForString(
    const std::string &text, std::map<std::string, std::shared_ptr<StringTexture>> &table,
    const StringTexture::Config &config) {
    auto it = table.find(text);
    if (it != table.end() && it->second) {
        return it->second;
    }
    auto texture = std::make_shared<StringTexture>(text, config);
    table[text] = texture;
    return texture;
}

std::shared_ptr<StringTexture> DisplaySlot::getTitleImage(
    std::map<std::string, std::shared_ptr<StringTexture>> &textureTable) {
    initStyles();
    if (mSetRef == nullptr) {
        return nullptr;
    }
    const std::string &title = mSetRef->mTruncTitleString;
    if (!mTitleImage && !title.empty() && title != mTitle) {
        bool isFolder = (mSetRef->mId != Shared::INVALID && mSetRef->mId != 0);
        mTitleImage = getTextureForString(title, textureTable, isFolder ? sCaptionStyle : sClusterStyle);
        mTitle = title;
    }
    return mTitleImage;
}

std::shared_ptr<StringTexture> DisplaySlot::getLocationImage(
    std::map<std::string, std::shared_ptr<StringTexture>> &textureTable) {
    initStyles();
    if (mSetRef == nullptr || mSetRef->mTitleString.empty()) {
        return nullptr;
    }
    // The original kicked off a reverse geocode here. There is none, because
    // turning a photograph's coordinates into a place name means handing them
    // to somebody else. So the name is never filled in, and everything that
    // hangs off it - this label, the slot's location button, the filter it
    // opens - is guarded on the name being there and stays out of the way.
    if (!mLocationImage && mSetRef->mReverseGeocodedLocationComputed &&
        !mSetRef->mReverseGeocodedLocation.empty()) {
        mLocation = mSetRef->mReverseGeocodedLocation;
        mLocationImage = getTextureForString(mLocation, textureTable, sLocationStyle);
    }
    return mLocationImage;
}
