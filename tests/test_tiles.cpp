// The arithmetic the tiled fullscreen view lays a picture out with.
//
// Both ends of it fail silently. A grid one level too coarse draws a blurry
// picture, which looks the same as a slow decode. And a rectangle one pixel past
// the edge of the original is refused by Android's region decoder rather than
// trimmed, so that corner never arrives at all.
#include "tests.h"

#include "graphics/TiledImage.h"

TEST(the_grid_is_as_coarse_as_the_screen_allows) {
    // A 4000 pixel picture drawn 1000 wide. Halving it twice leaves 1000, which
    // is still everything the screen can show; halving a third time would leave
    // 500 and lose detail.
    const TiledImage::Grid grid = TiledImage::gridFor(4000, 3000, 1000.0f);
    CHECK_EQ(grid.sampleSize, 4);
    CHECK_EQ(grid.regionEdge, 4 * TiledImage::kTileEdge);
}

TEST(a_picture_drawn_larger_than_it_is_uses_every_pixel) {
    // Zoomed past one to one. There is no finer level than the original.
    const TiledImage::Grid grid = TiledImage::gridFor(2095, 3000, 3572.0f);
    CHECK_EQ(grid.sampleSize, 1);
    CHECK_EQ(grid.regionEdge, TiledImage::kTileEdge);
}

TEST(halving_stops_before_it_loses_detail) {
    // The boundary. Drawn one pixel narrower than half the original, one more
    // halving is still enough; one pixel wider and it is not.
    CHECK_EQ(TiledImage::gridFor(4096, 4096, 2048.0f).sampleSize, 2);
    CHECK_EQ(TiledImage::gridFor(4096, 4096, 2049.0f).sampleSize, 1);
}

TEST(the_grid_covers_the_whole_picture) {
    // Six tiles over a picture that is two and a bit regions wide and just
    // under two deep, so the last column and row hold the remainder.
    const TiledImage::Grid grid = TiledImage::gridFor(3000, 2000, 1322.0f);
    CHECK_EQ(grid.sampleSize, 2);
    CHECK_EQ(grid.regionEdge, 1024);
    CHECK_EQ(grid.columns, 3);
    CHECK_EQ(grid.rows, 2);

    // Every tile together accounts for the picture exactly: no gap, no overlap.
    int covered = 0;
    for (int row = 0; row < grid.rows; ++row) {
        for (int column = 0; column < grid.columns; ++column) {
            const TiledImage::Region region = TiledImage::regionFor(grid, 3000, 2000, column, row);
            covered += region.width * region.height;
        }
    }
    CHECK_EQ(covered, 3000 * 2000);
}

TEST(an_edge_tile_stops_at_the_edge) {
    // The last column of the same grid starts at 2048 and the picture ends at
    // 3000, so the rectangle asked for is 952 wide rather than a full 1024. A
    // full width one would run off the picture, and a region decoder refuses
    // that rather than trimming it.
    const TiledImage::Grid grid = TiledImage::gridFor(3000, 2000, 1322.0f);
    const TiledImage::Region corner = TiledImage::regionFor(grid, 3000, 2000, grid.columns - 1, grid.rows - 1);
    CHECK_EQ(corner.x, 2048);
    CHECK_EQ(corner.y, 1024);
    CHECK_EQ(corner.width, 3000 - 2048);
    CHECK_EQ(corner.height, 2000 - 1024);
    CHECK(corner.x + corner.width <= 3000);
    CHECK(corner.y + corner.height <= 2000);
}

TEST(a_tile_comes_back_at_its_size_divided_by_the_sample) {
    const TiledImage::Grid grid = TiledImage::gridFor(3000, 2000, 1322.0f);
    const TiledImage::Region inner = TiledImage::regionFor(grid, 3000, 2000, 0, 0);
    CHECK_EQ(inner.width, 1024);
    CHECK_EQ(Bitmap::sampledRegionSize(inner.width, inner.height, grid.sampleSize).width, TiledImage::kTileEdge);

    // A short edge tile divides by the same sample, rounded down as Android's
    // region decoder rounds, so it lands on the picture at the same scale as
    // its neighbours rather than being stretched to fill a whole tile.
    const TiledImage::Region edge = TiledImage::regionFor(grid, 3000, 2000, grid.columns - 1, 0);
    CHECK_EQ(edge.width, 952);
    CHECK_EQ(Bitmap::sampledRegionSize(edge.width, edge.height, grid.sampleSize).width, 952 / grid.sampleSize);

    // Never zero, however thin the remainder is. Here the picture ends two
    // pixels past the last whole tile while four of its pixels go into one, so
    // the division alone would give an image no pixels wide.
    const TiledImage::Grid sliver = TiledImage::gridFor(4098, 4098, 1000.0f);
    CHECK_EQ(sliver.sampleSize, 4);
    const TiledImage::Region last = TiledImage::regionFor(sliver, 4098, 4098, sliver.columns - 1, 0);
    CHECK_EQ(last.width, 2);
    CHECK_EQ(Bitmap::sampledRegionSize(last.width, last.height, sliver.sampleSize).width, 1);
}

TEST(a_region_outside_the_grid_is_empty_rather_than_wrong) {
    const TiledImage::Grid grid = TiledImage::gridFor(2095, 3000, 1322.0f);
    CHECK_EQ(TiledImage::regionFor(grid, 2095, 3000, grid.columns, 0).width, 0);
    CHECK_EQ(TiledImage::regionFor(grid, 2095, 3000, 0, -1).height, 0);
}

TEST(an_unknown_size_makes_no_grid) {
    // An item whose source never said how big its original is. The view has to
    // fall back to the screennail rather than lay a grid over a guess.
    CHECK_EQ(TiledImage::gridFor(0, 0, 1000.0f).columns, 0);
    CHECK_EQ(TiledImage::gridFor(4000, 3000, 0.0f).columns, 0);
}

TEST(a_huge_picture_is_still_one_screen_of_tiles) {
    // A 9310 pixel picture drawn one to one is a grid far past any texture
    // size limit. What bounds the work is the screen, not the picture.
    const TiledImage::Grid grid = TiledImage::gridFor(9310, 6237, 9310.0f);
    CHECK_EQ(grid.sampleSize, 1);
    CHECK_EQ(grid.columns, 19);
    CHECK_EQ(grid.rows, 13);

    // A 2560 pixel wide window over that grid touches six columns at most, so
    // a screenful is a couple of dozen tiles however large the original is.
    const int columnsOnScreen = (2560 + grid.regionEdge - 1) / grid.regionEdge + 1;
    CHECK(columnsOnScreen <= 7);
}
