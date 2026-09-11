// The window buttons the app draws for itself.
//
// These exist because the caption is no longer the system's to draw. If the
// close button lands in the wrong place, or the region that drags the window
// covers it, the window cannot be closed with the pointer at all - and that is
// not something a screenshot check would notice.
#include "tests.h"

#include "App.h"
#include "Bitmap.h"
#include "CaptionButtons.h"
#include "HudLayer.h"

namespace {

struct ScopedDensity {
    explicit ScopedDensity(float density) : previous(App::UI_DENSITY) {
        App::UI_DENSITY = density;
    }
    ~ScopedDensity() {
        App::UI_DENSITY = previous;
    }
    float previous;
};

// How much ink sits in a vertical band of the composed strip.
int inkIn(const Bitmap &bitmap, int fromX, int toX) {
    int count = 0;
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = fromX; x < toX && x < bitmap.width(); ++x) {
            if (bitmap.pixels()[((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4 + 3] != 0) {
                ++count;
            }
        }
    }
    return count;
}

// Whether two composed strips differ anywhere in a vertical band.
bool bandsDiffer(const Bitmap &a, const Bitmap &b, int fromX, int toX) {
    if (a.width() != b.width() || a.height() != b.height()) {
        return true;
    }
    for (int y = 0; y < a.height(); ++y) {
        for (int x = fromX; x < toX && x < a.width(); ++x) {
            const size_t at = ((size_t)y * (size_t)a.width() + (size_t)x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                if (a.pixels()[at + channel] != b.pixels()[at + channel]) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Lays the strip out at the window's top right, the way HudLayer does. Taken by
// reference because the widget cannot be copied: its texture reads state back
// out of it while drawing.
void layOut(CaptionButtons &buttons, float windowWidth) {
    buttons.setSize(CaptionButtons::preferredWidth(), CaptionButtons::preferredHeight());
    buttons.setPosition(windowWidth - CaptionButtons::preferredWidth(), 0.0f);
}

}  // namespace

TEST(the_strip_is_three_buttons_wide_at_any_density) {
    {
        ScopedDensity density(1.0f);
        CHECK_NEAR(CaptionButtons::preferredWidth(), 138.0f, 0.001f);
        CHECK_NEAR(CaptionButtons::preferredHeight(), 32.0f, 0.001f);
    }
    {
        ScopedDensity density(2.0f);
        CHECK_NEAR(CaptionButtons::preferredWidth(), 276.0f, 0.001f);
        CHECK_NEAR(CaptionButtons::preferredHeight(), 64.0f, 0.001f);
    }
}

TEST(each_button_answers_for_its_own_third) {
    ScopedDensity density(1.0f);
    const float windowWidth = 1280.0f;
    CaptionButtons buttons;
    layOut(buttons, windowWidth);

    const float left = windowWidth - CaptionButtons::preferredWidth();
    // Close is the rightmost, which is what makes the top right corner of the
    // window close it.
    CHECK(buttons.containsPoint(windowWidth - 1.0f, 1.0f));
    CHECK(buttons.containsPoint(left + 1.0f, 1.0f));

    // And nothing outside the strip.
    CHECK(!buttons.containsPoint(left - 1.0f, 1.0f));
    CHECK(!buttons.containsPoint(windowWidth - 1.0f, CaptionButtons::preferredHeight() + 1.0f));
}

TEST(all_three_buttons_draw_something) {
    ScopedDensity density(1.0f);
    CaptionButtons buttons;
    layOut(buttons, 1280.0f);
    Bitmap strip = buttons.compose();
    CHECK(strip.valid());
    if (!strip.valid()) {
        return;
    }

    const int third = (int)(CaptionButtons::preferredWidth() / 3.0f);
    // Each button has its glyph, and none of them is empty. A missing glyph is
    // a button that looks like blank chrome.
    CHECK(inkIn(strip, 0, third) > 0);
    CHECK(inkIn(strip, third, third * 2) > 0);
    CHECK(inkIn(strip, third * 2, third * 3) > 0);
}

TEST(the_maximise_glyph_changes_when_the_window_is_maximised) {
    ScopedDensity density(1.0f);
    CaptionButtons buttons;
    layOut(buttons, 1280.0f);
    const int third = (int)(CaptionButtons::preferredWidth() / 3.0f);

    Bitmap restored = buttons.compose();
    buttons.setMaximized(true);
    Bitmap maximized = buttons.compose();

    CHECK(restored.valid() && maximized.valid());
    if (!restored.valid() || !maximized.valid()) {
        return;
    }

    // The middle button has to say something different, or it claims the window
    // is in a state it is not. Checked as "these pixels differ" rather than by
    // counting ink: the two glyphs cover almost exactly the same area, so a
    // count says they are the same drawing when they are not.
    CHECK(bandsDiffer(restored, maximized, third, third * 2));

    // And only that button. The other two mean the same thing either way, and
    // redrawing them would be a wasted upload as well as a surprise.
    CHECK(!bandsDiffer(restored, maximized, 0, third));
    CHECK(!bandsDiffer(restored, maximized, third * 2, third * 3));
}

TEST(hovering_close_paints_it_rather_than_only_the_glyph) {
    ScopedDensity density(1.0f);
    const float windowWidth = 1280.0f;
    CaptionButtons buttons;
    layOut(buttons, windowWidth);
    const int third = (int)(CaptionButtons::preferredWidth() / 3.0f);

    const int quiet = inkIn(buttons.compose(), third * 2, third * 3);

    // The pointer over the close button, which is the one feedback a window
    // button has to give before it is clicked.
    buttons.onPointerMoved(windowWidth - 2.0f, 2.0f);
    Bitmap hovered = buttons.compose();
    const int lit = inkIn(hovered, third * 2, third * 3);
    CHECK(lit > quiet);

    // Red, not a grey wash, so it reads as the destructive one.
    const size_t sample = ((size_t)2 * (size_t)hovered.width() + (size_t)(third * 2 + 2)) * 4;
    CHECK(hovered.pixels()[sample] > hovered.pixels()[sample + 1]);
    CHECK(hovered.pixels()[sample] > hovered.pixels()[sample + 2]);

    // And the pointer moving away puts it back.
    buttons.onPointerMoved(10.0f, 10.0f);
    CHECK_EQ(inkIn(buttons.compose(), third * 2, third * 3), quiet);
}

TEST(the_top_bar_swallows_a_drag_instead_of_scrolling_the_wall) {
    ScopedDensity density(1.0f);
    HudLayer hud;
    hud.setSize(1280.0f, 800.0f);

    // Anywhere along the strip, not only where a crumb happens to be. The path
    // bar is only as wide as its crumbs, so without the HUD claiming the rest a
    // drag across the empty part of the bar reached the wall and scrolled it.
    CHECK(hud.containsPoint(20.0f, 4.0f));
    CHECK(hud.containsPoint(640.0f, 4.0f));
    CHECK(hud.containsPoint(1270.0f, 4.0f));

    // And it is a strip, not the window: the wall below it still takes drags.
    CHECK(!hud.containsPoint(640.0f, 400.0f));
    CHECK(!hud.containsPoint(640.0f, 799.0f));

    // A drag that lands in the strip is consumed, which is what stops it
    // reaching the layer behind.
    MotionEvent down;
    down.action = MotionEvent::ACTION_DOWN;
    down.xs[0] = 640.0f;
    down.ys[0] = 4.0f;
    CHECK(hud.onTouchEvent(down));
}

TEST(a_faded_out_bar_swallows_nothing) {
    ScopedDensity density(1.0f);
    HudLayer hud;
    hud.setSize(1280.0f, 800.0f);
    CHECK(hud.containsPoint(640.0f, 4.0f));

    // In fullscreen the chrome fades away and the photo underneath wants the
    // whole window. A strip that kept eating drags after it went invisible
    // would be a dead band across the top of the picture.
    hud.setAlpha(0.0f);
    CHECK(!hud.containsPoint(640.0f, 4.0f));
}

TEST(the_selection_bar_starts_below_the_window_buttons) {
    // It spans the whole width, so where the path bar only reaches its own
    // crumbs this one reaches the corner. Drawn at the very top it would cover
    // the close button, which is the only way to shut the window with a
    // pointer.
    CHECK_NEAR(HudLayer::selectionBarTop(0.0f, 34.0f), 34.0f, 0.001f);

    // With an ordinary title bar there is no caption of ours to clear, so it
    // sits where it always did.
    CHECK_NEAR(HudLayer::selectionBarTop(0.0f, 0.0f), 0.0f, 0.001f);

    // And a cutout deeper than the caption wins, because a control under a
    // notch is one you cannot reliably touch.
    CHECK_NEAR(HudLayer::selectionBarTop(50.0f, 34.0f), 50.0f, 0.001f);
}
