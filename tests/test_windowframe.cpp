// WindowFrame on every platform before a window has one: nothing is extended
// and nothing is taken from the top of the window. The tests never make a
// window, so what install does to one is checked with screenshots instead.
#include "tests.h"

#include "app/WindowFrame.h"

TEST(a_window_frame_that_is_not_installed_takes_no_room) {
    CHECK(!WindowFrame::isExtended());
    CHECK_EQ(WindowFrame::captionHeight(), 0.0f);
    CHECK(!WindowFrame::install(nullptr));
    CHECK(!WindowFrame::isExtended());
    CHECK_EQ(WindowFrame::captionHeight(), 0.0f);
}
