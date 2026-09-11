// The wash the wall sits on, built from the photo under the cursor.
//
// Only the pure half is here. What actually broke it was wiring: the backdrop
// asked a thumbnail texture to load itself a second time, and once decoding
// went asynchronous a texture's load() returned nothing at all. That is caught
// by the screenshot check - three albums, three different backdrops - because
// no arithmetic here was wrong.
#include "tests.h"

#include "AdaptiveBackgroundTexture.h"
#include "Bitmap.h"

namespace {

Bitmap solid(int width, int height, uint8_t red, uint8_t green, uint8_t blue) {
    Bitmap bitmap(width, height);
    uint8_t *pixels = bitmap.pixels();
    for (int i = 0; i < width * height; ++i) {
        pixels[i * 4] = red;
        pixels[i * 4 + 1] = green;
        pixels[i * 4 + 2] = blue;
        pixels[i * 4 + 3] = 255;
    }
    return bitmap;
}

// The colour halfway down the left edge, where the backdrop is still opaque.
// The right edge fades out so the stitched copies blend, and reading there
// would measure the fade rather than the colour.
void leftEdgeColour(const Bitmap &backdrop, int *red, int *green, int *blue) {
    const uint8_t *pixels = backdrop.pixels();
    const size_t row = (size_t)(backdrop.height() / 2) * (size_t)backdrop.width();
    *red = pixels[row * 4];
    *green = pixels[row * 4 + 1];
    *blue = pixels[row * 4 + 2];
}

}  // namespace

TEST(the_backdrop_takes_its_colour_from_the_photo) {
    const int width = 256;
    const int height = 128;
    Bitmap warm = AdaptiveBackgroundTexture::backdropFrom(solid(128, 96, 200, 40, 30), width, height);
    Bitmap cool = AdaptiveBackgroundTexture::backdropFrom(solid(128, 96, 30, 40, 200), width, height);
    CHECK(warm.valid());
    CHECK(cool.valid());
    CHECK_EQ(warm.width(), width);
    CHECK_EQ(warm.height(), height);

    int wr, wg, wb, cr, cg, cb;
    leftEdgeColour(warm, &wr, &wg, &wb);
    leftEdgeColour(cool, &cr, &cg, &cb);
    CHECK(wr > wb);
    CHECK(cb > cr);

    // Darkened on the way, which is what keeps the wall readable on top of it.
    // The original multiplied by 0xaa.
    CHECK(wr < 200);
    CHECK(cb < 200);
}

TEST(the_backdrop_fades_out_on_its_right) {
    // BackgroundLayer stitches three copies with a quarter of their width
    // overlapping, so without the fade the seams would be visible bands.
    Bitmap backdrop = AdaptiveBackgroundTexture::backdropFrom(solid(128, 96, 180, 180, 180), 256, 128);
    CHECK(backdrop.valid());
    const uint8_t *pixels = backdrop.pixels();
    const size_t row = (size_t)(backdrop.height() / 2) * (size_t)backdrop.width();
    const int leftAlpha = pixels[row * 4 + 3];
    const int rightAlpha = pixels[(row + (size_t)backdrop.width() - 1) * 4 + 3];
    CHECK(leftAlpha > 200);
    CHECK(rightAlpha < 40);
}

TEST(a_photo_that_never_arrived_makes_no_backdrop) {
    // The failure this has to survive: the decode answered with nothing. The
    // fallback gradient shows instead, which is what an invalid bitmap asks the
    // render view for.
    CHECK(!AdaptiveBackgroundTexture::backdropFrom(Bitmap(), 256, 128).valid());
    CHECK(!AdaptiveBackgroundTexture::backdropFrom(solid(128, 96, 10, 10, 10), 0, 128).valid());
}
