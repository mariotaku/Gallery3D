// Turning a string into pixels, which is the one part of drawing the platform
// is better at than we are. It knows which font the system reads in, and which
// other fonts to reach for when that one has no glyph for what was asked, so
// Japanese or emoji come out as themselves rather than as boxes.
//
// Canvas draws through this. CMake picks the implementation: the platform's own
// where there is one worth having, SDL_ttf everywhere else.
#pragma once

#include <cstddef>
#include <string>

#include "graphics/Bitmap.h"

namespace TextBackend {

// Opens whatever the backend draws with. Called once on the main thread,
// before any text, and shutdown on the same thread after the last.
bool init();
void shutdown();
bool ready();

// What the text would measure drawn at fontSize pixels to the em: the width of
// the run, trailing spaces included, and the height of the line box, which is
// the same for every string at one size and weight. The line box is the
// platform's own: DirectWrite's includes the font's line gap, and Android's and
// SDL_ttf's are ascent plus descent, so Windows lines are about 13% taller. The
// fonts differ too, so no size is compared between platforms. False when there is
// nothing to draw with or fontSize is not above zero, and then neither output
// is written. An empty string measures true with no width. Callable from
// several threads at once. tests/test_text.cpp checks this on every platform.
bool measure(const std::string &text, float fontSize, bool bold, int *width, int *height);

// The glyphs as straight white, with their coverage in the alpha channel, at
// exactly the size measure reports. The caller tints them, so the colour here
// is never seen. Invalid when measure would be false or the width is zero.
Bitmap render(const std::string &text, float fontSize, bool bold);

}  // namespace TextBackend
