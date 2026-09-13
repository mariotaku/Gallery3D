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

// Opens whatever the backend draws with. Called once, before any text.
bool init();
void shutdown();
bool ready();

// What the text would measure drawn at this size, in pixels. False if there is
// nothing to draw with, and then neither output is written.
bool measure(const std::string &text, float fontSize, bool bold, int *width, int *height);

// The glyphs in white, their coverage in the alpha channel. The caller tints
// them, so the colour here is never seen.
Bitmap render(const std::string &text, float fontSize, bool bold);

}  // namespace TextBackend
