// Density bucket selection and nine-patch guide parsing, both against the art
// the port actually ships.
#include "tests.h"

#include <string>

#include "app/App.h"
#include "graphics/Bitmap.h"
#include "graphics/Canvas.h"
#include "graphics/DrawableLoad.h"

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

struct Bucket {
    const char *directory;
    float density;
};

const Bucket kBuckets[] = {
    {"drawable-mdpi", 1.0f},   {"drawable-hdpi", 1.5f},    {"drawable-xhdpi", 2.0f},
    {"drawable-xxhdpi", 3.0f}, {"drawable-xxxhdpi", 4.0f},
};

}  // namespace

TEST(each_bucket_density_takes_its_own_bucket) {
    ScopedAssetRoot root;
    for (const Bucket &bucket : kBuckets) {
        ScopedDensity density(bucket.density);
        App::Drawable drawable = App::findDrawable("icon_home_small");
        CHECK(endsWith(drawable.path, std::string(bucket.directory) + "/icon_home_small.png"));
        CHECK_NEAR(drawable.density, bucket.density, 0.001);
    }
}

TEST(a_density_between_buckets_takes_the_one_above) {
    ScopedAssetRoot root;
    {
        ScopedDensity density(1.75f);
        CHECK(endsWith(App::findDrawable("icon_home_small").path, "drawable-xhdpi/icon_home_small.png"));
    }
    {
        // A common phone density.
        ScopedDensity density(2.625f);
        CHECK(endsWith(App::findDrawable("icon_home_small").path, "drawable-xxhdpi/icon_home_small.png"));
    }
}

TEST(chrome_is_never_enlarged_up_to_the_top_bucket) {
    ScopedAssetRoot root;
    for (int hundredths = 100; hundredths <= 400; hundredths += 5) {
        ScopedDensity density((float)hundredths / 100.0f);
        CHECK(App::drawableBucketDensity() >= App::UI_DENSITY - 0.001f);
        CHECK(App::findDrawable("pathbar_cap").density >= App::UI_DENSITY - 0.001f);
    }
}

TEST(chrome_art_is_the_size_its_code_draws_it_at) {
    ScopedAssetRoot root;
    // The sizes the path bar, menu bar and popup draw these into. At a bucket's
    // density they have to match to the pixel, or the art is resampled and
    // goes soft.
    struct Expected {
        const char *name;
        float width;
        float height;
    };
    const Expected expected[] = {
        {"pathbar_cap", 22.0f, 39.0f},         {"pathbar_join", 21.0f, 39.0f},
        {"icon_home_small", 39.0f, 39.0f},     {"icon_more", 34.0f, 34.0f},
        {"ic_menu_rotate_left", 34.0f, 34.0f}, {"popup_triangle_bottom", 43.0f, 28.0f},
        {"selection_menu_bg_pressed_left", 21.0f, 58.0f},
    };
    for (const Bucket &bucket : kBuckets) {
        ScopedDensity density(bucket.density);
        for (const Expected &art : expected) {
            Bitmap bitmap = Bitmap::load(App::drawablePath(art.name), 0);
            CHECK(bitmap.valid());
            CHECK_EQ(bitmap.width(), App::uiPixels(art.width));
            CHECK_EQ(bitmap.height(), App::uiPixels(art.height));
        }
    }
}

TEST(art_with_no_bucket_falls_back_to_the_plain_folder) {
    ScopedAssetRoot root;
    ScopedDensity density(2.625f);
    // The wall's own textures ship only in the unqualified folder.
    App::Drawable drawable = App::findDrawable("stack_frame");
    CHECK(endsWith(drawable.path, "drawable/stack_frame.png"));
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
        ScopedDensity density(4.5f);
        // Past every bucket, so the densest available and an accepted upscale.
        CHECK_NEAR(App::drawableBucketDensity(), 4.0, 0.001);
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

TEST(a_drawable_reports_the_density_its_pixels_are_drawn_for) {
    // Whichever bucket, or scaling, a platform chooses, the pixels divided by
    // the density they are drawn for are the art's size at 1x.
    ScopedAssetRoot root;
    DrawableLoad::Result baseline;
    {
        ScopedDensity density(1.0f);
        baseline = DrawableLoad::load("icon_home_small");
    }
    CHECK(baseline.bitmap.valid());
    for (float wanted : {1.5f, 2.0f, 3.0f, 4.0f}) {
        ScopedDensity density(wanted);
        const DrawableLoad::Result result = DrawableLoad::load("icon_home_small");
        CHECK(result.bitmap.valid());
        CHECK(result.density > 0.0f);
        if (!result.bitmap.valid() || !baseline.bitmap.valid() || result.density <= 0.0f) {
            continue;
        }
        CHECK_NEAR(result.bitmap.width() / result.density, baseline.bitmap.width() / baseline.density, 1.0);
        CHECK_NEAR(result.bitmap.height() / result.density, baseline.bitmap.height() / baseline.density, 1.0);
    }
}

TEST(a_nine_patch_source_is_the_art_at_its_density_with_a_stretch_region) {
    ScopedAssetRoot root;
    ScopedDensity density(2.0f);
    const DrawableLoad::NinePatchSource source = DrawableLoad::loadNinePatch("popup.9");
    CHECK(source.bitmap.valid());
    CHECK(source.density > 0.0f);
    if (!source.bitmap.valid() || source.density <= 0.0f) {
        return;
    }
    // The content is 62 wide at 1x, whether the guide border is still on it
    // or the platform took it off.
    const int border = source.hasGuides ? 2 : 0;
    CHECK_NEAR((source.bitmap.width() - border) / source.density, 62.0, 1.0);
    if (!source.hasGuides) {
        CHECK(source.stretchX1 > source.stretchX0);
        CHECK(source.stretchY1 > source.stretchY0);
        CHECK(source.stretchX1 <= source.bitmap.width());
        CHECK(source.stretchY1 <= source.bitmap.height());
    }
}
