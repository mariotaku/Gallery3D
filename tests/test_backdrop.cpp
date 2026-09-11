// The wash the wall sits on, built from the photo under the cursor.
//
// Only the pure half is here. What actually broke it was wiring: the backdrop
// asked a thumbnail texture to load itself a second time, and once decoding
// went asynchronous a texture's load() returned nothing at all. That is caught
// by the screenshot check - three albums, three different backdrops - because
// no arithmetic here was wrong.
#include "tests.h"

#include <cmath>
#include <vector>

#include "AdaptiveBackgroundTexture.h"
#include "App.h"
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

// Puts a blur kind back the way it was, so the order the tests run in cannot
// matter.
struct ScopedBlur {
    ScopedBlur(int kind, float sigma) : previousKind(App::BACKDROP_BLUR), previousSigma(App::BACKDROP_BLUR_SIGMA) {
        App::BACKDROP_BLUR = kind;
        App::BACKDROP_BLUR_SIGMA = sigma;
    }
    ~ScopedBlur() {
        App::BACKDROP_BLUR = previousKind;
        App::BACKDROP_BLUR_SIGMA = previousSigma;
    }
    int previousKind;
    float previousSigma;
};

// A hard vertical edge, black into white, which is what shows the shape of a
// blur most plainly.
Bitmap stepEdge(int width, int height) {
    Bitmap bitmap(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t value = (x < width / 2) ? 0 : 255;
            uint8_t *pixel = bitmap.pixels() + ((size_t)y * (size_t)width + (size_t)x) * 4;
            pixel[0] = pixel[1] = pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return bitmap;
}

// The brightness across the middle of a backdrop.
std::vector<int> middleRow(const Bitmap &backdrop) {
    std::vector<int> row;
    const uint8_t *pixels = backdrop.pixels();
    const size_t start = (size_t)(backdrop.height() / 2) * (size_t)backdrop.width();
    for (int x = 0; x < backdrop.width(); ++x) {
        row.push_back(pixels[(start + (size_t)x) * 4]);
    }
    return row;
}

// How far a step edge spreads, measured between the tenth and ninetieth
// percentile of the change.
int edgeSpread(const std::vector<int> &row) {
    int low = 255;
    int high = 0;
    for (int value : row) {
        low = (value < low) ? value : low;
        high = (value > high) ? value : high;
    }
    const int span = high - low;
    int first = -1;
    int last = -1;
    for (size_t i = 0; i < row.size(); ++i) {
        if (first < 0 && row[i] > low + span / 10) {
            first = (int)i;
        }
        if (row[i] > low + (span * 9) / 10) {
            last = (int)i;
            break;
        }
    }
    return (first >= 0 && last >= first) ? (last - first) : 0;
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

TEST(the_backdrop_fades_out_whatever_shape_the_photo_is) {
    // The one that was wrong. BackgroundLayer stitches the backdrop with a
    // quarter of its width overlapping, and the fade has to be that quarter
    // whatever the photo looked like.
    //
    // The crop is as wide as the photo allows, so a portrait photo makes a
    // narrower one - 89 pixels against a landscape photo's 128. With the fade
    // written as a pixel index of 96 it fell outside the narrow crop and was
    // skipped, and the backdrop ended with a hard vertical edge down the wall.
    struct Shape {
        const char *name;
        int width;
        int height;
    };
    const Shape shapes[] = {{"landscape", 128, 96}, {"portrait", 89, 128}, {"square", 128, 128},
                            {"panorama", 128, 32}};
    for (const Shape &shape : shapes) {
        Bitmap backdrop = AdaptiveBackgroundTexture::backdropFrom(solid(shape.width, shape.height, 180, 150, 90), 256,
                                                                 128);
        CHECK(backdrop.valid());
        if (!backdrop.valid()) {
            continue;
        }
        const uint8_t *pixels = backdrop.pixels();
        const size_t row = (size_t)(backdrop.height() / 2) * (size_t)backdrop.width();
        CHECK(pixels[row * 4 + 3] > 200);
        CHECK(pixels[(row + (size_t)backdrop.width() - 1) * 4 + 3] < 40);

        // And the fade is a quarter of the width, not some other slice:
        // everything left of three quarters across is fully opaque, and it is
        // still opaque at the last pixel before the fade starts rather than
        // already a few percent down, which would leave a faint line where one
        // copy of the backdrop meets the next.
        int dipped = 0;
        for (int x = 0; x < backdrop.width() * 3 / 4 - 2; ++x) {
            if (pixels[(row + (size_t)x) * 4 + 3] < 254) {
                ++dipped;
            }
        }
        CHECK_EQ(dipped, 0);
    }
}

TEST(a_photo_that_never_arrived_makes_no_backdrop) {
    // The failure this has to survive: the decode answered with nothing. The
    // fallback gradient shows instead, which is what an invalid bitmap asks the
    // render view for.
    CHECK(!AdaptiveBackgroundTexture::backdropFrom(Bitmap(), 256, 128).valid());
    CHECK(!AdaptiveBackgroundTexture::backdropFrom(solid(128, 96, 10, 10, 10), 0, 128).valid());
}

TEST(both_blurs_spread_a_colour_about_as_far) {
    // Turning the gaussian on is meant to change the shape of the falloff, not
    // how much wash there is. Its default strength is the box blur's own spread
    // - a nine tap box has variance (81 - 1) / 12, whose root is 2.58 - so the
    // two have to land close to each other.
    std::vector<int> box;
    std::vector<int> gaussian;
    {
        ScopedBlur blur(App::BACKDROP_BLUR_BOX, 2.58f);
        box = middleRow(AdaptiveBackgroundTexture::backdropFrom(stepEdge(89, 128), 256, 128));
    }
    {
        ScopedBlur blur(App::BACKDROP_BLUR_GAUSSIAN, 2.58f);
        gaussian = middleRow(AdaptiveBackgroundTexture::backdropFrom(stepEdge(89, 128), 256, 128));
    }
    const int boxSpread = edgeSpread(box);
    const int gaussianSpread = edgeSpread(gaussian);
    CHECK(boxSpread > 8);
    // Within a fifth. Measured they are 21 and 19, and the tolerance is tight
    // on purpose: a gaussian whose kernel is cut short comes out narrower than
    // its sigma says, and lands at 14.
    CHECK(std::abs(gaussianSpread - boxSpread) <= boxSpread / 5);
}

TEST(the_gaussian_has_no_corner_where_the_blur_ends) {
    // What the gaussian is for. A box gives a straight ramp that stops dead at
    // each end, and those two corners are what the eye picks out as a ring
    // around a bright patch. A gaussian tails off instead, so the change is
    // packed into the middle and the ends are gentle.
    //
    // Measured as the share of the whole step that falls in the middle third of
    // the transition. A straight ramp spends a third of itself there; anything
    // with tails spends more.
    auto middleShare = [](const std::vector<int> &row) {
        int low = 255;
        int high = 0;
        for (int value : row) {
            low = (value < low) ? value : low;
            high = (value > high) ? value : high;
        }
        const int span = high - low;
        if (span <= 0) {
            return 0.0;
        }
        // The transition, taken generously so both kinds are measured over the
        // same window.
        int first = -1;
        int last = -1;
        for (size_t i = 0; i < row.size(); ++i) {
            if (first < 0 && row[i] > low + span / 50) {
                first = (int)i;
            }
            if (row[i] < high - span / 50) {
                last = (int)i;
            }
        }
        const int width = last - first;
        if (width < 6) {
            return 0.0;
        }
        const int third = width / 3;
        return (double)(row[(size_t)(first + 2 * third)] - row[(size_t)(first + third)]) / (double)span;
    };

    double box = 0.0;
    double gaussian = 0.0;
    {
        ScopedBlur blur(App::BACKDROP_BLUR_BOX, 2.58f);
        box = middleShare(middleRow(AdaptiveBackgroundTexture::backdropFrom(stepEdge(89, 128), 256, 128)));
    }
    {
        ScopedBlur blur(App::BACKDROP_BLUR_GAUSSIAN, 2.58f);
        gaussian = middleShare(middleRow(AdaptiveBackgroundTexture::backdropFrom(stepEdge(89, 128), 256, 128)));
    }
    CHECK(box > 0.0);
    CHECK(gaussian > box);
}

TEST(a_stronger_sigma_blurs_further) {
    // The knob has to do something, and in the direction the help text claims.
    std::vector<int> gentle;
    std::vector<int> strong;
    {
        ScopedBlur blur(App::BACKDROP_BLUR_GAUSSIAN, 1.0f);
        gentle = middleRow(AdaptiveBackgroundTexture::backdropFrom(stepEdge(89, 128), 256, 128));
    }
    {
        ScopedBlur blur(App::BACKDROP_BLUR_GAUSSIAN, 6.0f);
        strong = middleRow(AdaptiveBackgroundTexture::backdropFrom(stepEdge(89, 128), 256, 128));
    }
    CHECK(edgeSpread(strong) > edgeSpread(gentle) * 2);
}

TEST(a_sigma_of_nothing_still_makes_a_backdrop) {
    // The bottom of the range. No blur at all is a legitimate setting to tune
    // to, and it must not divide by zero on the way.
    ScopedBlur blur(App::BACKDROP_BLUR_GAUSSIAN, 0.0f);
    Bitmap backdrop = AdaptiveBackgroundTexture::backdropFrom(stepEdge(89, 128), 256, 128);
    CHECK(backdrop.valid());
    if (backdrop.valid()) {
        // Still faded on its right, because that is not the blur's doing.
        const uint8_t *pixels = backdrop.pixels();
        const size_t row = (size_t)(backdrop.height() / 2) * (size_t)backdrop.width();
        CHECK(pixels[(row + (size_t)backdrop.width() - 1) * 4 + 3] < 40);
    }
}
