// Runs the window's content up under the title bar, keeping the system frame.
//
// The same idea as NSWindow's fullSizeContentView on macOS: the caption stops
// being a strip of chrome above the content and becomes part of the content,
// with the app drawing whatever belongs there.
//
// This is not a borderless window. The frame is still real, so snapping, the
// resize borders, the drop shadow, double click to maximise and the right click
// system menu all keep working, and none of it has to be reimplemented. What
// goes is the caption bar itself, and with it the minimise, maximise and close
// buttons, which the app now draws.
//
// Windows only so far. Everywhere else install() says so and the window keeps
// its ordinary title bar.
#pragma once

struct SDL_Window;

namespace WindowFrame {

// Takes the caption area into the client area. Returns false where the platform
// has no equivalent, leaving the window alone.
bool install(SDL_Window *window);

// True once install has succeeded, so the layout knows whether the top of the
// window is its own to use.
bool isExtended();

// How tall the caption strip is, in window coordinates. Content may be drawn
// under it, but anything meant to be clicked should stay clear of the buttons.
float captionHeight();

// How much of the top right the window buttons take. Chrome laid out at the top
// right has to start left of this.
float captionButtonsWidth();

}  // namespace WindowFrame
