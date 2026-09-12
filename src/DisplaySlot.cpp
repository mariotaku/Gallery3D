#include "DisplaySlot.h"

#include "App.h"
#include "GridDrawables.h"
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
    int labelWidth = GridDrawables::labelTextureWidth();
    int labelHeight = GridDrawables::labelTextureHeight();

    // A fixed box and a fixed font, so every album label comes out the same
    // size. A title too long for the box loses its tail to an ellipsis rather
    // than dragging the whole label down a few points.
    sCaptionStyle.sizeMode = StringTexture::Config::SIZE_EXACT;
    sCaptionStyle.overflowMode = StringTexture::Config::OVERFLOW_ELLIPSIZE;
    sCaptionStyle.fontSize = GridDrawables::labelFontSize();
    sCaptionStyle.bold = true;
    sCaptionStyle.width = labelWidth;
    sCaptionStyle.height = labelHeight;
    sCaptionStyle.yalignment = StringTexture::Config::ALIGN_TOP;
    sCaptionStyle.xalignment = StringTexture::Config::ALIGN_HCENTER;
    sCaptionStyle.superSample = 2;

    sClusterStyle = sCaptionStyle;

    sLocationStyle = sCaptionStyle;
    sLocationStyle.fontSize = 0.75f * GridDrawables::labelFontSize();
    sLocationStyle.bold = false;
    sLocationStyle.yalignment = StringTexture::Config::ALIGN_VCENTER;

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
    // The untruncated title: the label box measures the string and ellipsizes
    // what will not fit. mTruncTitleString has already cut the name to sixteen
    // characters and put an ellipsis of its own in, which the box would then
    // cut again, ending the label on six dots.
    const std::string &title = mSetRef->mTitleString;
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
    // No reverse geocoder: location names remain empty, hiding location labels, buttons and
    // filters.
    if (!mLocationImage && mSetRef->mReverseGeocodedLocationComputed &&
        !mSetRef->mReverseGeocodedLocation.empty()) {
        mLocation = mSetRef->mReverseGeocodedLocation;
        mLocationImage = getTextureForString(mLocation, textureTable, sLocationStyle);
    }
    return mLocationImage;
}
