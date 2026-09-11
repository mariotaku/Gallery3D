// The HUD has to keep its controls inside the safe rect, because a control
// under a cutout or a home indicator is one you cannot reliably touch. The wall
// behind it deliberately does not, which is why only the HUD is checked here.
#include "tests.h"

#include "App.h"
#include "HudLayer.h"
#include "MenuBar.h"
#include "PathBarLayer.h"
#include "TimeBar.h"

namespace {

const float kWindowWidth = 1280.0f;
const float kWindowHeight = 800.0f;

struct ScopedSafeArea {
    ScopedSafeArea(float left, float top, float right, float bottom) : previous(App::SAFE_AREA) {
        App::SAFE_AREA.left = left;
        App::SAFE_AREA.top = top;
        App::SAFE_AREA.right = right;
        App::SAFE_AREA.bottom = bottom;
    }
    ~ScopedSafeArea() {
        App::SAFE_AREA = previous;
    }
    App::SafeAreaInsets previous;
};

// Everything the HUD can put in front of the user, as rects.
struct Bounds {
    float left;
    float top;
    float right;
    float bottom;
};

Bounds boundsOf(const Layer &layer) {
    return Bounds{layer.getX(), layer.getY(), layer.getX() + layer.getWidth(),
                  layer.getY() + layer.getHeight()};
}

void checkInsideSafeArea(const char *what, const Bounds &bounds, const App::SafeAreaInsets &safe) {
    (void)what;
    CHECK(bounds.left >= safe.left - 0.5f);
    CHECK(bounds.top >= safe.top - 0.5f);
    CHECK(bounds.right <= kWindowWidth - safe.right + 0.5f);
    CHECK(bounds.bottom <= kWindowHeight - safe.bottom + 0.5f);
}

}  // namespace

TEST(hud_keeps_every_control_inside_the_safe_area) {
    // A cutout at the top and a home indicator at the bottom, the usual shape.
    ScopedSafeArea safeArea(0.0f, 90.0f, 0.0f, 60.0f);
    HudLayer hud;
    hud.setSize(kWindowWidth, kWindowHeight);

    checkInsideSafeArea("path bar", boundsOf(*hud.getPathBar()), App::SAFE_AREA);
    checkInsideSafeArea("menu bar", boundsOf(*hud.getMenuBar()), App::SAFE_AREA);
    checkInsideSafeArea("selection bar", boundsOf(*hud.getSelectionMenuTop()), App::SAFE_AREA);
    checkInsideSafeArea("fullscreen bar", boundsOf(*hud.getFullscreenMenu()), App::SAFE_AREA);
    checkInsideSafeArea("time bar", boundsOf(*hud.getTimeBar()), App::SAFE_AREA);
}

TEST(hud_respects_insets_on_the_sides_too) {
    // A landscape phone puts the cutout on one side and the indicator on the
    // other, so the left and right insets have to be honoured independently.
    ScopedSafeArea safeArea(120.0f, 0.0f, 45.0f, 0.0f);
    HudLayer hud;
    hud.setSize(kWindowWidth, kWindowHeight);

    checkInsideSafeArea("path bar", boundsOf(*hud.getPathBar()), App::SAFE_AREA);
    checkInsideSafeArea("menu bar", boundsOf(*hud.getMenuBar()), App::SAFE_AREA);
    checkInsideSafeArea("selection bar", boundsOf(*hud.getSelectionMenuTop()), App::SAFE_AREA);

    // Not merely inside it, but actually moved: a layout that ignored the inset
    // and happened to fit would pass the check above.
    CHECK_NEAR(hud.getMenuBar()->getX(), 120.0, 0.5);
    CHECK_NEAR(hud.getMenuBar()->getWidth(), kWindowWidth - 120.0 - 45.0, 0.5);
}

TEST(no_insets_puts_the_hud_back_on_the_window_edges) {
    ScopedSafeArea safeArea(0.0f, 0.0f, 0.0f, 0.0f);
    HudLayer hud;
    hud.setSize(kWindowWidth, kWindowHeight);

    // A desktop reports no insets, and nothing should move because of this.
    CHECK_NEAR(hud.getMenuBar()->getX(), 0.0, 0.5);
    CHECK_NEAR(hud.getMenuBar()->getWidth(), kWindowWidth, 0.5);
    CHECK_NEAR(hud.getMenuBar()->getY() + hud.getMenuBar()->getHeight(), kWindowHeight, 0.5);
    CHECK_NEAR(hud.getSelectionMenuTop()->getY(), 0.0, 0.5);
}

TEST(the_bottom_bar_sits_directly_on_the_safe_edge) {
    const float bottomInset = 60.0f;
    ScopedSafeArea safeArea(0.0f, 0.0f, 0.0f, bottomInset);
    HudLayer hud;
    hud.setSize(kWindowWidth, kWindowHeight);

    // Flush with the safe edge, not floating above it and not overlapping it.
    const MenuBar *bar = hud.getMenuBar();
    CHECK_NEAR(bar->getY() + bar->getHeight(), kWindowHeight - bottomInset, 0.5);
}
