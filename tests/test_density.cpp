// What has to change when the window lands on a display of a different scale.
//
// The window keeps its size across a move between monitors, so the layout is
// asked to run again with the same box and different units. That is the case
// worth pinning: everything here is in density units, and a check that only
// ever runs at density 1 would pass whatever the arithmetic did.
#include "tests.h"

#include "App.h"
#include "GridLayer.h"
#include "GridLayoutInterface.h"

namespace {

// Puts the globals back however the test leaves them, so one case cannot
// change what the next one measures.
struct DensityGuard {
    DensityGuard() : pixel(App::PIXEL_DENSITY), ui(App::UI_DENSITY) {}
    ~DensityGuard() {
        App::PIXEL_DENSITY = pixel;
        App::UI_DENSITY = ui;
    }
    float pixel;
    float ui;
};

}  // namespace

TEST(the_cell_grows_with_the_wall_density) {
    DensityGuard guard;

    App::PIXEL_DENSITY = 1.0f;
    const int narrowWidth = GridLayer::itemWidthForDensity();
    const int narrowHeight = GridLayer::itemHeightForDensity();
    CHECK_EQ(narrowWidth, 96);
    CHECK_EQ(narrowHeight, 72);

    App::PIXEL_DENSITY = 2.25f;
    CHECK_EQ(GridLayer::itemWidthForDensity(), 216);
    CHECK_EQ(GridLayer::itemHeightForDensity(), 162);

    // The cell keeps its shape, which is what stops the wall from stretching
    // when it is rebuilt at a new density.
    const float narrowAspect = (float)narrowWidth / (float)narrowHeight;
    const float wideAspect = (float)GridLayer::itemWidthForDensity() / (float)GridLayer::itemHeightForDensity();
    CHECK_NEAR(wideAspect, narrowAspect, 0.001f);
}

TEST(slot_spacing_follows_a_density_change) {
    DensityGuard guard;

    App::PIXEL_DENSITY = 1.0f;
    GridLayoutInterface layout(4);

    Vector3f before;
    layout.getPositionForSlotIndex(4, 96, 72, before);

    // The same object, told the display changed underneath it. Spacing is fixed
    // at construction otherwise, so a move between monitors would leave the
    // gaps at the old scale while the cells grew.
    App::PIXEL_DENSITY = 2.0f;
    layout.onDensityChanged();

    Vector3f after;
    layout.getPositionForSlotIndex(4, 192, 144, after);

    // Slot 4 is one column over with four rows, so its x is one cell plus one
    // gap. Both doubled, so the position does too.
    CHECK_NEAR(after.x, before.x * 2.0f, 0.001f);
}

TEST(a_density_change_that_is_no_change_leaves_the_spacing_alone) {
    DensityGuard guard;

    App::PIXEL_DENSITY = 1.5f;
    GridLayoutInterface layout(4);
    Vector3f before;
    layout.getPositionForSlotIndex(4, 144, 108, before);

    layout.onDensityChanged();
    Vector3f after;
    layout.getPositionForSlotIndex(4, 144, 108, after);
    CHECK_NEAR(after.x, before.x, 0.001f);
    CHECK_NEAR(after.y, before.y, 0.001f);
}

TEST(the_chrome_does_not_take_the_walls_enlargement) {
    DensityGuard guard;

    // The two densities are separate on purpose. CONTENT_SCALE makes the wall
    // bigger than the phone it was laid out for; a button should be the size
    // the screen asks for and no more. Getting this wrong made every control
    // too big, which is how the split came about.
    App::UI_DENSITY = 1.5f;
    App::PIXEL_DENSITY = 1.5f * App::CONTENT_SCALE;
    CHECK(App::PIXEL_DENSITY > App::UI_DENSITY);
    CHECK_NEAR(App::PIXEL_DENSITY / App::UI_DENSITY, App::CONTENT_SCALE, 0.001f);
}

TEST(the_drawable_bucket_follows_the_chrome_density) {
    DensityGuard guard;

    // Art is shipped at 1x and 1.5x. A display between them takes the higher
    // bucket and scales down, which is what keeps an icon from going blocky.
    App::UI_DENSITY = 1.0f;
    CHECK_NEAR(App::drawableBucketDensity(), 1.0f, 0.001f);

    App::UI_DENSITY = 1.5f;
    CHECK_NEAR(App::drawableBucketDensity(), 1.5f, 0.001f);

    App::UI_DENSITY = 1.25f;
    CHECK_NEAR(App::drawableBucketDensity(), 1.5f, 0.001f);

    // And past the top bucket there is nothing better to pick, so it stays.
    App::UI_DENSITY = 2.0f;
    CHECK_NEAR(App::drawableBucketDensity(), 1.5f, 0.001f);
}
