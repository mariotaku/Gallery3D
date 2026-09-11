// Stands in for android.graphics.Canvas and Paint; composes widgets into bitmaps.
// blend* takes straight alpha (including SDL_ttf glyphs); blit* takes premultiplied alpha.
// Destinations are premultiplied for GL_ONE / GL_ONE_MINUS_SRC_ALPHA.
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

// Replaces pixels, including alpha (PorterDuff.Mode.SRC). Used to replace the
// popup's bottom border with its triangle outline.
void stamp(Bitmap &dst, const Bitmap &src, int x, int y);

// Nine-patch caps stay fixed while the middle stretches. loadNinePatch strips
// the one-pixel guide border from shipped PNGs.
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

// Resolves a drawable's density bucket and scales art to PIXEL_DENSITY, including fixed-size
// caps.
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

// Draws an antialiased line of the given thickness.
void drawLine(Bitmap &dst, float x0, float y0, float x1, float y1, float thickness, float r, float g, float b,
              float a);

void fillRect(Bitmap &dst, int x, int y, int width, int height, float r, float g, float b, float a);

}  // namespace Canvas
