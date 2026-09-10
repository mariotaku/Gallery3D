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

// Replaces the destination pixels outright, alpha included, rather than
// blending over them. Stands in for PorterDuff.Mode.SRC. The popup triangle
// needs it: it has to cut the straight border off the bottom of the panel and
// put its own outline there, and blending would leave the border showing
// through.
void stamp(Bitmap &dst, const Bitmap &src, int x, int y);

// A nine-patch: the caps keep their size and the middle stretches. Android
// resolved these at build time; here the 1 pixel guide border survives into
// the shipped PNG, so loadNinePatch reads it and strips it.
struct NinePatch {
    Bitmap image;
    // Half open, in image coordinates. Everything outside is a cap.
    int stretchX0 = 0;
    int stretchX1 = 0;
    int stretchY0 = 0;
    int stretchY1 = 0;

    bool valid() const {
        return image.valid();
    }
};

// Takes a drawable name, not a path, because it resolves the density bucket and
// then resizes the art to PIXEL_DENSITY. That resize matters: the caps are
// drawn at their own size and only the middle stretches, so art left at the
// density it shipped for gives a panel with corners and borders too small for
// everything drawn next to them.
NinePatch loadNinePatch(const std::string &name);

// Draws a nine-patch into the rect. Smaller than the caps and it clamps, so a
// too small rect loses the middle rather than mangling the corners.
void blitNinePatch(Bitmap &dst, const NinePatch &patch, int x, int y, int width, int height, float alpha = 1.0f);

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
