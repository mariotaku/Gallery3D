// What has to change when the window lands on a display of a different scale.
//
// The window keeps its size across a move between monitors, so the layout is
// asked to run again with the same box and different units. That is the case
// worth pinning: everything here is in density units, and a check that only
// ever runs at density 1 would pass whatever the arithmetic did.
#include "tests.h"

#include "app/App.h"
#include "grid/GridDrawManager.h"
#include "grid/GridLayer.h"
#include "grid/GridLayoutInterface.h"
#include "graphics/Texture.h"

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

    // Chrome ships in Android's buckets, mdpi to xxxhdpi. A display at one of
    // their densities takes it; one between takes the one above and scales
    // down, so an icon is never enlarged.
    App::UI_DENSITY = 1.0f;
    CHECK_NEAR(App::drawableBucketDensity(), 1.0f, 0.001f);

    App::UI_DENSITY = 2.0f;
    CHECK_NEAR(App::drawableBucketDensity(), 2.0f, 0.001f);

    App::UI_DENSITY = 1.75f;
    CHECK_NEAR(App::drawableBucketDensity(), 2.0f, 0.001f);

    // And past the top bucket there is nothing better to pick, so it stays.
    App::UI_DENSITY = 5.0f;
    CHECK_NEAR(App::drawableBucketDensity(), 4.0f, 0.001f);
}

TEST(a_thumbnail_texture_takes_the_nearer_power_of_two) {
    // 128 units at 4.5 wants 576 pixels. Rounding up to 1024 holds three times
    // the pixels of a 512 the grid already draws smaller than.
    CHECK_EQ(thumbnailTextureEdge(128, 4.5f, 0), 512);
    // Past the midpoint it does round up, so a density that genuinely wants the
    // larger texture still gets it: 896 is nearer 1024 than 512.
    CHECK_EQ(thumbnailTextureEdge(128, 7.0f, 0), 1024);
    // Exactly halfway takes the smaller one. Either is the same distance from
    // what was asked for, and the smaller costs a quarter of the memory.
    CHECK_EQ(thumbnailTextureEdge(128, 6.0f, 0), 512);
    CHECK_EQ(thumbnailTextureEdge(128, 1.0f, 0), 128);
    CHECK_EQ(thumbnailTextureEdge(128, 2.0f, 0), 256);
}

TEST(a_device_can_ask_for_a_smaller_thumbnail_than_its_density_wants) {
    // What a slow or small-memory phone sets: the cap wins.
    CHECK_EQ(thumbnailTextureEdge(128, 4.5f, 256), 256);
    CHECK_EQ(thumbnailTextureEdge(128, 4.5f, 512), 512);
    // A cap above what the density asks for changes nothing, so raising it on a
    // low density screen does not enlarge a thumbnail past its source.
    CHECK_EQ(thumbnailTextureEdge(128, 2.0f, 1024), 256);
    // Zero is no cap at all.
    CHECK_EQ(thumbnailTextureEdge(128, 4.5f, 0), 512);
}

TEST(a_thumbnail_with_no_size_to_work_from_is_not_a_texture) {
    CHECK_EQ(thumbnailTextureEdge(0, 4.5f, 0), 0);
    CHECK_EQ(thumbnailTextureEdge(128, 0.0f, 0), 0);
}

TEST(a_thumbnail_decodes_just_large_enough_to_cover_its_crop) {
    // A landscape photo of the cell's own shape needs no more than the crop.
    CHECK_EQ(thumbnailDecodeEdge(512, 384, 4000, 3000), 512);
    // A portrait one covers the crop's width with its short edge.
    CHECK_EQ(thumbnailDecodeEdge(512, 384, 3000, 4000), 683);
    // A wide one covers the height, up to twice the crop's long edge.
    CHECK_EQ(thumbnailDecodeEdge(512, 384, 1920, 1080), 683);
    CHECK_EQ(thumbnailDecodeEdge(512, 384, 8000, 2000), 1024);
    // Unknown until the photo is read: twice the crop, as before.
    CHECK_EQ(thumbnailDecodeEdge(512, 384, 0, 0), 1024);
    // A photo smaller than the crop is asked for at the crop's size.
    CHECK_EQ(thumbnailDecodeEdge(512, 384, 100, 75), 512);
}

TEST(the_kept_slots_are_the_visible_ones_and_the_nearest_that_fit) {
    const auto tenEach = [](int) { return (size_t)10; };
    // 30 for the three visible slots, then one after, one before, and so on.
    IndexRange kept = GridDrawManager::keptSlots(IndexRange(10, 12), IndexRange(0, 40), 70, tenEach);
    CHECK_EQ(kept.begin, 8);
    CHECK_EQ(kept.end, 14);
    // The visible slots stay even past the allowance.
    kept = GridDrawManager::keptSlots(IndexRange(10, 12), IndexRange(0, 40), 20, tenEach);
    CHECK_EQ(kept.begin, 10);
    CHECK_EQ(kept.end, 12);
    // Never past the buffered slots.
    kept = GridDrawManager::keptSlots(IndexRange(0, 2), IndexRange(0, 5), 1000, tenEach);
    CHECK_EQ(kept.begin, 0);
    CHECK_EQ(kept.end, 5);
    // A slot too large to fit stops its side, and the other side goes on.
    kept = GridDrawManager::keptSlots(IndexRange(10, 12), IndexRange(0, 40), 70,
                                      [](int slot) { return slot == 13 ? (size_t)100 : (size_t)10; });
    CHECK_EQ(kept.begin, 6);
    CHECK_EQ(kept.end, 12);
}
