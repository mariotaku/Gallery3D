// Extends content under the title bar while retaining the system frame and window actions.
// Windows only; other platforms keep their ordinary title bar.
// The app draws caption buttons.
#pragma once

struct SDL_Window;

namespace WindowFrame {

// Takes the caption area into the client area. Returns false where the platform
// has no equivalent, leaving the window alone.
bool install(SDL_Window *window);

// True once install has succeeded, so the layout knows whether the top of the
// window is its own to use.
bool isExtended();

// How tall the caption strip is, in the window's pixels, which is what the
// layers lay out in. Content may be drawn under it, but anything meant to be
// clicked should stay clear of the buttons. 0 until install has succeeded, and
// on every platform without an extended frame. tests/test_windowframe.cpp
// checks that much; what install does to a window is checked with screenshots.
float captionHeight();

}  // namespace WindowFrame
