// Density bucket selection and nine-patch guide parsing, both against the art
// the port actually ships.
#include "tests.h"

#include <string>

#include "app/App.h"
#include "graphics/Canvas.h"

namespace {

// Chrome art follows UI_DENSITY, not PIXEL_DENSITY: a button is sized for the
// display, while PIXEL_DENSITY also carries the wall's enlargement. Restored on
// the way out so the order tests run in cannot matter.
struct ScopedDensity {
    explicit ScopedDensity(float density) : previous(App::UI_DENSITY) {
        App::UI_DENSITY = density;
    }
    ~ScopedDensity() {
        App::UI_DENSITY = previous;
    }
    float previous;
};

bool endsWith(const std::string &value, const std::string &suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct ScopedAssetRoot {
    ScopedAssetRoot() : previous(App::ASSET_ROOT) {
        App::ASSET_ROOT = GALLERY3D_ASSET_ROOT;
    }
    ~ScopedAssetRoot() {
        App::ASSET_ROOT = previous;
    }
    std::string previous;
};

}  // namespace

TEST(density_one_takes_the_mdpi_bucket) {
    ScopedAssetRoot root;
    ScopedDensity density(1.0f);
    App::Drawable drawable = App::findDrawable("icon_home_small");
    CHECK(endsWith(drawable.path, "drawable-mdpi/icon_home_small.png"));
    CHECK_NEAR(drawable.density, 1.0, 0.001);
}

TEST(a_higher_density_takes_hdpi) {
    ScopedAssetRoot root;
    ScopedDensity density(2.625f);
    App::Drawable drawable = App::findDrawable("icon_home_small");
    CHECK(endsWith(drawable.path, "drawable-hdpi/icon_home_small.png"));
    CHECK_NEAR(drawable.density, 1.5, 0.001);
}

TEST(art_with_no_bucket_falls_back_to_the_plain_folder) {
    ScopedAssetRoot root;
    ScopedDensity density(2.625f);
    // pathbar_bg ships only in the unqualified folder.
    App::Drawable drawable = App::findDrawable("pathbar_bg");
    CHECK(endsWith(drawable.path, "drawable/pathbar_bg.png"));
    CHECK_NEAR(drawable.density, 1.0, 0.001);
}

TEST(unscaled_textures_stay_on_the_plain_folder) {
    ScopedAssetRoot root;
    ScopedDensity density(2.625f);
    // Their callers were written against those exact pixel sizes, so the
    // bucket must not be upgraded underneath them.
    App::Drawable drawable = App::findDrawable("icon_home_small", false);
    CHECK(endsWith(drawable.path, "drawable/icon_home_small.png"));
    CHECK_NEAR(drawable.density, 1.0, 0.001);
}

TEST(the_reported_bucket_matches_what_is_chosen) {
    ScopedAssetRoot root;
    {
        ScopedDensity density(1.0f);
        CHECK_NEAR(App::drawableBucketDensity(), 1.0, 0.001);
    }
    {
        ScopedDensity density(1.25f);
        // Rounds up, so art is reduced rather than blown up.
        CHECK_NEAR(App::drawableBucketDensity(), 1.5, 0.001);
    }
    {
        ScopedDensity density(4.0f);
        // Past every bucket, so the densest available and an accepted upscale.
        CHECK_NEAR(App::drawableBucketDensity(), 1.5, 0.001);
    }
}

TEST(nine_patch_guides_are_stripped_and_scaled) {
    ScopedAssetRoot root;
    ScopedDensity density(1.0f);
    Canvas::NinePatch patch = Canvas::loadNinePatch("popup.9");
    CHECK(patch.valid());
    if (!patch.valid()) {
        return;
    }
    // Density 1 selects drawable-mdpi, which ships at 64x69 including the one
    // pixel guide border, so the content is 62x67.
    CHECK_EQ(patch.image.width(), 62);
    CHECK_EQ(patch.image.height(), 67);
    // The guides have to land strictly inside the content. Off by one here is
    // what turns the caps into a smear.
    CHECK(patch.stretchX0 > 0);
    CHECK(patch.stretchX1 > patch.stretchX0);
    CHECK(patch.stretchX1 < patch.image.width());
    CHECK(patch.stretchY0 > 0);
    CHECK(patch.stretchY1 > patch.stretchY0);
    CHECK(patch.stretchY1 < patch.image.height());
    // Caps on both sides, which is what keeps a corner its own size.
    CHECK(patch.image.width() - patch.stretchX1 > 1);
    CHECK(patch.image.height() - patch.stretchY1 > 1);
}

TEST(a_nine_patch_scales_to_the_density) {
    ScopedAssetRoot root;
    Canvas::NinePatch baseline;
    {
        ScopedDensity density(1.0f);
        baseline = Canvas::loadNinePatch("popup.9");
    }
    ScopedDensity density(2.0f);
    Canvas::NinePatch scaled = Canvas::loadNinePatch("popup.9");
    CHECK(scaled.valid());
    if (!baseline.valid() || !scaled.valid()) {
        return;
    }
    // Only the middle of a nine patch stretches, so art left at the density it
    // shipped for gives corners and borders too small for everything drawn
    // next to them.
    CHECK(scaled.image.width() > baseline.image.width());
    CHECK(scaled.image.height() > baseline.image.height());
    // The guides move with the art, or the stretched middle lands elsewhere.
    CHECK(scaled.stretchX0 > baseline.stretchX0);
}
