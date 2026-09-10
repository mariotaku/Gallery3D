// Stands in for android.graphics.Canvas and Paint: the 2D drawing the port
// needs in order to compose a widget into a bitmap before it becomes a texture.
//
// The original had a real Canvas to draw into. Here the primitives live in one
// place so both StringTexture and anything built on CanvasTexture can use them,
// instead of each rasterising text its own way.
//
// Alpha convention, which is easy to get wrong: SDL_ttf hands glyphs back with
// straight alpha, while the renderer blends with GL_ONE / GL_ONE_MINUS_SRC_ALPHA
// and therefore wants premultiplied. Anything named blend* takes a straight
// alpha source; anything named blit* takes a premultiplied one. Destinations
// are always premultiplied.
#pragma once

#include <cstddef>
#include <string>

#include "Bitmap.h"

namespace Canvas {

// Opens the shared fonts. Call once at startup, before any text is drawn.
bool initFonts();
void shutdownFonts();
bool fontsReady();

// Measures a string. Returns false when no font could be opened.
bool measureText(const std::string &text, float fontSize, bool bold, int *width, int *height);

// How many bytes of text fit within maxWidth. Used to truncate a label.
size_t lengthToFit(const std::string &text, float fontSize, bool bold, int maxWidth);

// Rasterises glyphs into a straight alpha bitmap. Composite it with blendOver
// rather than uploading it, or the antialiasing will be wrong.
Bitmap renderText(const std::string &text, float fontSize, bool bold);

// Blends a straight alpha source over a premultiplied destination, tinting by
// (r, g, b, a).
void blendOver(Bitmap &dst, const Bitmap &src, int x, int y, float r, float g, float b, float a);

// Blends a premultiplied source, which is what Bitmap::load returns.
void blit(Bitmap &dst, const Bitmap &src, int x, int y, float alpha = 1.0f);

// Stretches a premultiplied source across the rect. The path bar fill ships as
// a single column, so this is how it becomes a bar.
void blitScaled(Bitmap &dst, const Bitmap &src, int x, int y, int width, int height, float alpha = 1.0f);

// Box blurs the coverage of src and returns it as white with that coverage as
// its alpha, padded by radius so the halo is not clipped. Stands in for
// Paint.setShadowLayer, which drew a blurred drop shadow.
Bitmap blurredCoverage(const Bitmap &src, int radius);

// Draws text with an optional soft black halo behind it, so a label stays
// readable over a bright photo.
void drawText(Bitmap &dst, const std::string &text, int x, int y, float fontSize, bool bold, float r, float g,
              float b, float a, int shadowRadius);

void fillRect(Bitmap &dst, int x, int y, int width, int height, float r, float g, float b, float a);

}  // namespace Canvas
