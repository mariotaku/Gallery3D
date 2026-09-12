// Sizing the album label under a stack.
//
// The label is drawn into a texture and mapped onto a fixed quad, so its size
// on screen comes out of the arithmetic here rather than out of the draw. Each
// case below pins a way that arithmetic has already gone wrong once.
#include "tests.h"

#include "App.h"
#include "GridDrawables.h"
#include "GridLayer.h"
#include "Shared.h"

namespace {

// Puts the globals back however the test leaves them.
struct DensityGuard {
    DensityGuard() : pixel(App::PIXEL_DENSITY), ui(App::UI_DENSITY) {}
    ~DensityGuard() {
        App::PIXEL_DENSITY = pixel;
        App::UI_DENSITY = ui;
    }
    float pixel;
    float ui;
};

// Sets both densities the way main() does: the wall carries the content scale,
// the chrome does not.
void setDisplay(float displayScale, float contentScale) {
    App::UI_DENSITY = displayScale;
    App::PIXEL_DENSITY = displayScale * contentScale;
}

}  // namespace

TEST(the_label_is_the_same_size_whatever_the_wall_scale) {
    DensityGuard guard;

    // One display, three settings of wall.scale. The label is chrome-sized
    // text, so it may not move with any of them.
    setDisplay(2.0f, 1.0f);
    const float atOne = GridDrawables::labelFontSize();

    setDisplay(2.0f, 1.5f);
    CHECK_NEAR(GridDrawables::labelFontSize(), atOne, 0.001f);

    setDisplay(2.0f, 3.0f);
    CHECK_NEAR(GridDrawables::labelFontSize(), atOne, 0.001f);
}

TEST(the_label_follows_display_scale) {
    DensityGuard guard;

    // 16 of them, so a label is the same physical size on any screen.
    setDisplay(1.0f, 1.5f);
    CHECK_NEAR(GridDrawables::labelFontSize(), 16.0f, 0.001f);

    setDisplay(3.0f, 1.5f);
    CHECK_NEAR(GridDrawables::labelFontSize(), 48.0f, 0.001f);
}

TEST(both_sides_of_the_label_box_are_powers_of_two) {
    DensityGuard guard;

    // A box that is not one gets padded out to one before it is uploaded,
    // while the quad goes on sampling the whole texture, which draws the
    // padding as part of the label and shifts it off its stack.
    const float scales[] = {1.0f, 1.25f, 1.75f, 2.0f, 2.625f, 3.0f, 4.0f};
    for (float scale : scales) {
        setDisplay(scale, 1.5f);
        CHECK(Shared::isPowerOf2(GridDrawables::labelTextureWidth()));
        CHECK(Shared::isPowerOf2(GridDrawables::labelTextureHeight()));
    }
}

TEST(the_label_box_is_never_shorter_than_its_line) {
    DensityGuard guard;

    // A line stands taller than its font size, and the shadow around it adds
    // more, so the box needs about half as much again. Rounding the height
    // down to the nearer power of two saves memory and clips the descenders.
    const float kLineAndShadow = 1.6f;
    const float scales[] = {1.0f, 1.25f, 1.75f, 2.0f, 2.625f, 3.0f, 4.0f};
    for (float scale : scales) {
        setDisplay(scale, 1.5f);
        const float needed = kLineAndShadow * GridDrawables::labelFontSize();
        CHECK((float)GridDrawables::labelTextureHeight() >= needed);
    }
}

TEST(the_label_box_keeps_up_with_the_cell) {
    DensityGuard guard;

    // Width is measured in cells, not in pixels. A fixed pixel width is a
    // smaller and smaller share of the tile as density climbs, which is what
    // made long titles shrink on a handset and not on a desktop.
    setDisplay(1.0f, 1.5f);
    const int narrowCell = GridLayer::itemWidthForDensity();
    const int narrowBox = GridDrawables::labelTextureWidth();

    setDisplay(3.0f, 1.5f);
    const int wideCell = GridLayer::itemWidthForDensity();
    const int wideBox = GridDrawables::labelTextureWidth();

    CHECK(wideCell > narrowCell);
    CHECK(wideBox > narrowBox);

    // Within a power of two either way of the same share of a cell, which is
    // as close as rounding to a power of two allows.
    const float narrowShare = (float)narrowBox / (float)narrowCell;
    const float wideShare = (float)wideBox / (float)wideCell;
    CHECK(narrowShare > 0.75f && narrowShare < 3.0f);
    CHECK(wideShare > 0.75f && wideShare < 3.0f);
}

TEST(a_wider_cell_never_narrows_the_label_box) {
    DensityGuard guard;

    int previous = 0;
    for (float scale = 1.0f; scale <= 4.0f; scale += 0.25f) {
        setDisplay(scale, 1.5f);
        const int box = GridDrawables::labelTextureWidth();
        CHECK(box >= previous);
        previous = box;
    }
}
