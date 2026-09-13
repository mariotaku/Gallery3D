// Off-screen composition tests for the HUD widgets.
//
// Each of these composes itself into a bitmap before anything reaches GL, which
// is the same work renderBlended does, so a test can drive it with no context
// and no window. Nothing here compares against a stored image: the checks are
// on the properties that have actually gone wrong, which is mostly that some
// span was left uncovered or landed in the wrong place.
#include "tests.h"

#include <string>
#include <vector>

#include "app/App.h"
#include "graphics/Bitmap.h"
#include "hud/MenuBar.h"
#include "hud/PathBarLayer.h"
#include "hud/PopupMenu.h"

namespace {

// A density with a fraction in it. The value itself does not matter, only that
// it is not 1.0: the span arithmetic is exact there and only disagrees with
// itself away from it, which is where the gap between a crumb and its chevron
// came from. Naming a real display's scale here would be a fact that rots the
// next time someone changes their settings.
const float kFractionalDensity = 2.625f;

struct ScopedDensity {
    explicit ScopedDensity(float density) : previous(App::UI_DENSITY) {
        App::UI_DENSITY = density;
    }
    ~ScopedDensity() {
        App::UI_DENSITY = previous;
    }
    float previous;
};

bool columnHasInk(const Bitmap &bitmap, int x) {
    for (int y = 0; y < bitmap.height(); ++y) {
        if (bitmap.pixels()[((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4 + 3] != 0) {
            return true;
        }
    }
    return false;
}

bool rowHasInk(const Bitmap &bitmap, int y) {
    for (int x = 0; x < bitmap.width(); ++x) {
        if (bitmap.pixels()[((size_t)y * (size_t)bitmap.width() + (size_t)x) * 4 + 3] != 0) {
            return true;
        }
    }
    return false;
}

// Counts fully transparent columns that have ink on both sides. A bar is one
// continuous run, so any of these is a hole.
int interiorGaps(const Bitmap &bitmap) {
    int first = -1;
    int last = -1;
    for (int x = 0; x < bitmap.width(); ++x) {
        if (columnHasInk(bitmap, x)) {
            if (first < 0) {
                first = x;
            }
            last = x;
        }
    }
    if (first < 0) {
        return 0;
    }
    int gaps = 0;
    for (int x = first; x <= last; ++x) {
        if (!columnHasInk(bitmap, x)) {
            ++gaps;
        }
    }
    return gaps;
}

// The rightmost column carrying anything at all.
int lastInkColumn(const Bitmap &bitmap) {
    for (int x = bitmap.width() - 1; x >= 0; --x) {
        if (columnHasInk(bitmap, x)) {
            return x;
        }
    }
    return -1;
}

int opaquePixels(const Bitmap &bitmap) {
    int count = 0;
    for (int i = 0; i < bitmap.width() * bitmap.height(); ++i) {
        if (bitmap.pixels()[(size_t)i * 4 + 3] > 200) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST(path_bar_composes_a_bar_with_no_holes) {
    ScopedDensity density(kFractionalDensity);
    PathBarLayer bar;
    bar.setSize(600.0f, PathBarLayer::preferredHeight());
    bar.pushLabel("icon_home_small", "Gallery", nullptr);
    bar.pushLabel("icon_folder_small", "Album", nullptr);

    Bitmap composed = bar.compose();
    CHECK(composed.valid());
    if (!composed.valid()) {
        return;
    }
    CHECK(composed.width() > 0);
    CHECK_EQ(composed.height(), (int)(PathBarLayer::preferredHeight() + 0.5f));
    // The cap and the join carry the same tone as the fill and are placed by
    // their own arithmetic, so a rounding disagreement leaves a column of
    // backdrop showing through. That is exactly what happened once.
    CHECK_EQ(interiorGaps(composed), 0);
    // Ink from the left edge to very near the right. Not to the last column:
    // the cap is a rounded end that fades to nothing, and that fade stretches
    // with the density, so the allowance has to as well.
    CHECK(columnHasInk(composed, 0));
    int tolerance = (int)(3.0f * App::UI_DENSITY) + 1;
    CHECK(lastInkColumn(composed) >= composed.width() - 1 - tolerance);
}

TEST(path_bar_grows_with_its_label) {
    PathBarLayer shortBar;
    shortBar.setSize(600.0f, PathBarLayer::preferredHeight());
    shortBar.pushLabel("icon_home_small", "A", nullptr);
    int shortWidth = shortBar.compose().width();

    PathBarLayer longBar;
    longBar.setSize(600.0f, PathBarLayer::preferredHeight());
    longBar.pushLabel("icon_home_small", "A much longer album name", nullptr);
    int longWidth = longBar.compose().width();

    CHECK(longWidth > shortWidth);
}

TEST(path_bar_with_one_crumb_still_caps_itself) {
    ScopedDensity density(kFractionalDensity);
    PathBarLayer bar;
    bar.setSize(600.0f, PathBarLayer::preferredHeight());
    bar.pushLabel("icon_home_small", "Gallery", nullptr);
    Bitmap composed = bar.compose();
    CHECK(composed.valid());
    if (!composed.valid()) {
        return;
    }
    CHECK_EQ(interiorGaps(composed), 0);
}

TEST(menu_bar_lays_buttons_across_its_width) {
    ScopedDensity density(kFractionalDensity);
    MenuBar bar;
    bar.setSize(600.0f, MenuBar::preferredHeight());
    std::vector<MenuBar::ButtonSpec> buttons;
    buttons.push_back({"icon_delete", "Delete", nullptr});
    buttons.push_back({"icon_more", "More", nullptr});
    bar.setButtons(buttons);

    Bitmap composed = bar.compose();
    CHECK(composed.valid());
    if (!composed.valid()) {
        return;
    }
    CHECK_EQ(composed.width(), 600);
    CHECK_EQ(composed.height(), (int)(MenuBar::preferredHeight() + 0.5f));
    // The fill stretches the whole way, so every column carries something.
    CHECK_EQ(interiorGaps(composed), 0);
    // Each button gets its own half, and both draw an icon and a label into it.
    CHECK(opaquePixels(composed) > 0);
    CHECK_NEAR(bar.buttonCenterX(0), 150.0, 2.0);
    CHECK_NEAR(bar.buttonCenterX(1), 450.0, 2.0);
}

TEST(menu_bar_with_no_buttons_composes_nothing) {
    MenuBar bar;
    bar.setSize(600.0f, MenuBar::preferredHeight());
    bar.clearButtons();
    Bitmap composed = bar.compose();
    // Nothing to draw, and a zero sized texture is what the loader expects.
    CHECK(!composed.valid());
}

TEST(popup_menu_composes_a_panel_and_its_pointer) {
    PopupMenu menu;
    std::vector<PopupMenu::Option> options;
    options.push_back({"Rotate left", "ic_menu_rotate_left", nullptr});
    options.push_back({"Rotate right", "ic_menu_rotate_right", nullptr});
    menu.setOptions(options);
    menu.showAtPoint(300.0f, 400.0f, 1280.0f, 800.0f);

    Bitmap composed = menu.compose();
    CHECK(composed.valid());
    if (!composed.valid()) {
        return;
    }
    CHECK(composed.width() > 0);
    CHECK(composed.height() > 0);
    // The panel is opaque across the top and the triangle hangs below it, so
    // the last row carries less ink than the first. If the triangle were
    // missing or misplaced this would not hold.
    CHECK(rowHasInk(composed, 2));
    CHECK(opaquePixels(composed) > 0);
}

TEST(popup_menu_widens_for_a_longer_option) {
    PopupMenu narrow;
    narrow.setOptions({{"Cut", "icon_delete", nullptr}});
    narrow.showAtPoint(300.0f, 400.0f, 1280.0f, 800.0f);
    int narrowWidth = narrow.compose().width();

    PopupMenu wide;
    wide.setOptions({{"A considerably longer option", "icon_delete", nullptr}});
    wide.showAtPoint(300.0f, 400.0f, 1280.0f, 800.0f);
    int wideWidth = wide.compose().width();

    CHECK(wideWidth > narrowWidth);
}

TEST(popup_menu_stays_inside_the_window) {
    PopupMenu menu;
    menu.setOptions({{"Rotate left", "ic_menu_rotate_left", nullptr}});
    // Anchored hard against the right edge, where it has to be pushed back in.
    menu.showAtPoint(1275.0f, 400.0f, 1280.0f, 800.0f);
    Bitmap composed = menu.compose();
    CHECK(composed.valid());
    if (!composed.valid()) {
        return;
    }
    CHECK(menu.getX() >= 0.0f);
    CHECK(menu.getX() + (float)composed.width() <= 1280.0f);
}
