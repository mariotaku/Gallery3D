// What surrounds a fullscreen picture: the shadow outside its edges and the
// checkerboard behind its transparent pixels.
#include "tests.h"

#include <algorithm>

#include "Bitmap.h"
#include "GridDrawables.h"

namespace {

Bitmap opaque(int width, int height) {
    Bitmap bitmap(width, height);
    uint8_t *pixels = bitmap.pixels();
    for (int i = 0; i < width * height; ++i) {
        pixels[i * 4] = 40;
        pixels[i * 4 + 1] = 80;
        pixels[i * 4 + 2] = 120;
        pixels[i * 4 + 3] = 255;
    }
    return bitmap;
}

int alphaAt(const Bitmap &bitmap, int x, int y) {
    return bitmap.pixels()[((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4 + 3];
}

float area(float xMin, float yMin, float xMax, float yMax) {
    return std::max(0.0f, xMax - xMin) * std::max(0.0f, yMax - yMin);
}

float overlap(const GridDrawables::ShadowPiece &piece, float left, float bottom, float right, float top) {
    return area(std::max(piece.xMin, left), std::max(piece.yMin, bottom), std::min(piece.xMax, right),
                std::min(piece.yMax, top));
}

}  // namespace

TEST(transparency_is_found_in_a_single_pixel) {
    Bitmap bitmap = opaque(8, 8);
    CHECK(!bitmap.hasTransparency());
    bitmap.pixels()[(8 * 8 - 1) * 4 + 3] = 254;
    CHECK(bitmap.hasTransparency());
}

TEST(shadow_is_strongest_at_the_picture_edge_and_gone_at_the_rim) {
    const Bitmap shadow = GridDrawables::shadowBitmap(64, 0.4f);
    CHECK_EQ(shadow.width(), 64);
    // The texels either side of the middle sit on the picture's corner.
    CHECK_NEAR(alphaAt(shadow, 31, 31), 0.4f * 255.0f, 2.0f);
    CHECK_EQ(alphaAt(shadow, 0, 0), 0);
    CHECK_EQ(alphaAt(shadow, 63, 63), 0);
    CHECK(alphaAt(shadow, 31, 0) <= 1);
    // Fading all the way out from the edge, never coming back.
    for (int y = 1; y < 32; ++y) {
        CHECK(alphaAt(shadow, 31, y) >= alphaAt(shadow, 31, y - 1));
    }
    // Black, premultiplied, so only alpha carries anything.
    const uint8_t *pixels = shadow.pixels();
    bool black = true;
    for (int i = 0; i < 64 * 64; ++i) {
        black = black && pixels[i * 4] == 0 && pixels[i * 4 + 1] == 0 && pixels[i * 4 + 2] == 0;
    }
    CHECK(black);
}

TEST(shadow_pieces_ring_the_picture_without_covering_it) {
    const float left = -0.4f;
    const float bottom = -0.3f;
    const float right = 0.5f;
    const float top = 0.6f;
    const float radius = 0.05f;
    const std::array<GridDrawables::ShadowPiece, 8> pieces =
        GridDrawables::shadowPieces(left, bottom, right, top, radius);

    float total = 0.0f;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const GridDrawables::ShadowPiece &piece = pieces[i];
        total += area(piece.xMin, piece.yMin, piece.xMax, piece.yMax);
        CHECK_NEAR(overlap(piece, left, bottom, right, top), 0.0f, 1e-6f);
        for (size_t j = i + 1; j < pieces.size(); ++j) {
            CHECK_NEAR(overlap(piece, pieces[j].xMin, pieces[j].yMin, pieces[j].xMax, pieces[j].yMax), 0.0f, 1e-6f);
        }
    }
    // No overlap and the ring's whole area means no gap either.
    const float ring = area(left - radius, bottom - radius, right + radius, top + radius) -
                       area(left, bottom, right, top);
    CHECK_NEAR(total, ring, 1e-5f);

    // Every side that meets the picture samples the texture's middle, where
    // the shadow is strongest, and every outer side samples its rim.
    for (const GridDrawables::ShadowPiece &piece : pieces) {
        if (piece.xMax == left || piece.xMin == right) {
            CHECK_EQ(piece.xMax == left ? piece.uAtXMax : piece.uAtXMin, 0.5f);
            CHECK_EQ(piece.xMax == left ? piece.uAtXMin : piece.uAtXMax, piece.xMax == left ? 0.0f : 1.0f);
        }
        if (piece.yMax == bottom || piece.yMin == top) {
            CHECK_EQ(piece.yMax == bottom ? piece.vAtYMax : piece.vAtYMin, 0.5f);
            CHECK_EQ(piece.yMax == bottom ? piece.vAtYMin : piece.vAtYMax, piece.yMax == bottom ? 0.0f : 1.0f);
        }
    }
}

TEST(checker_alternates_two_opaque_greys) {
    const Bitmap checker = GridDrawables::checkerBitmap();
    CHECK_EQ(checker.width(), 2);
    CHECK_EQ(checker.height(), 2);
    CHECK(!checker.hasTransparency());
    const uint8_t *pixels = checker.pixels();
    CHECK_EQ((int)pixels[0], (int)pixels[12]);
    CHECK_EQ((int)pixels[4], (int)pixels[8]);
    CHECK((int)pixels[0] != (int)pixels[4]);
}
