// TextBackend, through Canvas, on every platform: sizes that agree with the
// pixels, glyphs in straight white, and the answers for nothing to draw. The
// fonts differ between platforms, so no width or height is compared across
// them. A string texture keeps its box however many times it reloads.
#include "tests.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "graphics/Canvas.h"
#include "graphics/Texture.h"

TEST(a_rendered_string_is_the_size_it_measures) {
    for (bool bold : {false, true}) {
        for (const char *text : {"Hg", "Gallery 3D", "trailing ", "\xE3\x81\x82\xE3\x81\x84"}) {
            const std::string what = std::string("\"") + text + "\"" + (bold ? " bold" : "");
            int width = 0;
            int height = 0;
            CHECK_DETAIL(Canvas::measureText(text, 20.0f, bold, &width, &height), what + ": did not measure");
            const Bitmap glyphs = Canvas::renderText(text, 20.0f, bold);
            CHECK_DETAIL(glyphs.valid() && glyphs.width() == width && glyphs.height() == height,
                         what + ": measured " + std::to_string(width) + "x" + std::to_string(height) +
                             ", drew " + std::to_string(glyphs.width()) + "x" + std::to_string(glyphs.height()));
        }
    }
}

TEST(glyphs_are_straight_white_with_their_coverage_in_alpha) {
    for (bool bold : {false, true}) {
        const Bitmap glyphs = Canvas::renderText("Hg", 32.0f, bold);
        CHECK(glyphs.valid());
        if (!glyphs.valid()) {
            continue;
        }
        int covered = 0;
        int partial = 0;
        int notWhite = 0;
        for (int i = 0; i < glyphs.width() * glyphs.height(); ++i) {
            const uint8_t *pixel = glyphs.pixels() + (size_t)i * 4;
            if (pixel[3] == 0) {
                continue;
            }
            ++covered;
            partial += pixel[3] < 255 ? 1 : 0;
            if (pixel[0] != 255 || pixel[1] != 255 || pixel[2] != 255) {
                ++notWhite;
            }
        }
        CHECK(covered > 0);
        // Antialiased edges, which a premultiplied backend would darken.
        CHECK(partial > 0);
        CHECK_DETAIL(notWhite == 0, std::to_string(notWhite) + " covered pixels are not straight white");
    }
}

TEST(every_string_at_one_size_measures_one_height) {
    int first = 0;
    int second = 0;
    CHECK(Canvas::measureText("ace", 20.0f, false, nullptr, &first));
    CHECK(Canvas::measureText("Hgjy|", 20.0f, false, nullptr, &second));
    CHECK_EQ(first, second);
    // And a larger size is at least as tall and as wide.
    int smallWidth = 0;
    int largeWidth = 0;
    int largeHeight = 0;
    CHECK(Canvas::measureText("Gallery", 12.0f, false, &smallWidth, nullptr));
    CHECK(Canvas::measureText("Gallery", 24.0f, false, &largeWidth, &largeHeight));
    CHECK(largeWidth > smallWidth);
    CHECK(largeHeight > first / 2);
}

TEST(no_size_draws_nothing_and_an_empty_string_has_no_width) {
    int width = 7;
    int height = 7;
    CHECK(!Canvas::measureText("Hg", 0.0f, false, &width, &height));
    CHECK(!Canvas::measureText("Hg", -3.0f, false, &width, &height));
    CHECK(!Canvas::renderText("Hg", 0.0f, false).valid());
    CHECK(Canvas::measureText("", 20.0f, false, &width, &height));
    CHECK_EQ(width, 0);
    CHECK(!Canvas::renderText("", 20.0f, false).valid());
}

TEST(a_longer_string_is_never_narrower) {
    const std::string text = "Gallery 3D";
    int previous = 0;
    for (size_t length = 1; length <= text.size(); ++length) {
        int width = 0;
        CHECK(Canvas::measureText(text.substr(0, length), 20.0f, false, &width, nullptr));
        CHECK_DETAIL(width >= previous, text.substr(0, length) + " measures " + std::to_string(width));
        previous = width;
    }
}

TEST(fitting_a_string_never_cuts_a_character_in_half) {
    const std::string text = "ab\xE3\x81\x82\xE3\x81\x84" "cd";
    int full = 0;
    CHECK(Canvas::measureText(text, 20.0f, false, &full, nullptr));
    for (int maxWidth = 0; maxWidth <= full + 2; ++maxWidth) {
        const size_t length = Canvas::lengthToFit(text, 20.0f, false, maxWidth);
        CHECK(length <= text.size());
        CHECK_DETAIL(length == text.size() || (text[length] & 0xC0) != 0x80,
                     "cut inside a character at " + std::to_string(maxWidth));
        if (length > 0) {
            int width = 0;
            CHECK(Canvas::measureText(text.substr(0, length), 20.0f, false, &width, nullptr));
            CHECK_DETAIL(width <= maxWidth, "does not fit " + std::to_string(maxWidth));
        }
    }
    CHECK_EQ(Canvas::lengthToFit(text, 20.0f, false, full), text.size());
}

TEST(a_supersampled_string_texture_reloads_at_the_same_size) {
    StringTexture::Config config;
    config.fontSize = 20.0f;
    config.width = 256;
    config.height = 64;
    config.sizeMode = StringTexture::Config::SIZE_EXACT;
    config.superSample = 2;
    StringTexture texture("Gallery 3D", config);
    // RenderView sets a texture's size from the bitmap it uploads, and an
    // evicted texture keeps that size for the meshes, so each reload after the
    // first finds the supersampled size there.
    for (int load = 0; load < 3; ++load) {
        const Bitmap bitmap = texture.load(nullptr);
        CHECK(bitmap.valid());
        if (!bitmap.valid()) {
            return;
        }
        CHECK_DETAIL(bitmap.width() == 512 && bitmap.height() == 128,
                     "load " + std::to_string(load) + " drew " + std::to_string(bitmap.width()) + "x" +
                         std::to_string(bitmap.height()));
        texture.mWidth = bitmap.width();
        texture.mHeight = bitmap.height();
    }
}

TEST(text_draws_from_eight_threads_at_once) {
    const Bitmap expected = Canvas::renderText("Thread 3D", 24.0f, false);
    CHECK(expected.valid());
    if (!expected.valid()) {
        return;
    }
    std::atomic<int> differing{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 8; ++thread) {
        threads.emplace_back([&]() {
            for (int round = 0; round < 20; ++round) {
                const Bitmap glyphs = Canvas::renderText("Thread 3D", 24.0f, false);
                const bool same = glyphs.valid() && glyphs.width() == expected.width() &&
                                  glyphs.height() == expected.height() &&
                                  std::equal(glyphs.pixels(),
                                             glyphs.pixels() + (size_t)glyphs.width() * glyphs.height() * 4,
                                             expected.pixels());
                if (!same) {
                    ++differing;
                }
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }
    CHECK_EQ(differing.load(), 0);
}
