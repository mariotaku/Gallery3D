#include "graphics/Canvas.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "app/App.h"
#include "graphics/DrawableLoad.h"
#include "graphics/TextBackend.h"

namespace {

// Where each byte of a source pixel goes in a destination pixel: straight
// across when the two share an order, with red and blue exchanged when not.
struct ChannelMap {
    int to[4];
};

ChannelMap channelMap(const Bitmap &src, const Bitmap &dst) {
    if (src.order() == dst.order()) {
        return {{0, 1, 2, 3}};
    }
    return {{2, 1, 0, 3}};
}

}  // namespace

namespace Canvas {

bool initFonts() {
    return TextBackend::init();
}

void shutdownFonts() {
    TextBackend::shutdown();
}

bool fontsReady() {
    return TextBackend::ready();
}

bool measureText(const std::string &text, float fontSize, bool bold, int *width, int *height) {
    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
    return TextBackend::measure(text, fontSize, bold, width, height);
}

size_t lengthToFit(const std::string &text, float fontSize, bool bold, int maxWidth) {
    if (maxWidth <= 0) {
        return 0;
    }
    int w = 0;
    if (!measureText(text, fontSize, bold, &w, nullptr)) {
        return 0;
    }
    if (w <= maxWidth) {
        return text.length();
    }
    // Walk back until it fits. Step over whole UTF-8 sequences so a multibyte
    // character is never cut in half. The measuring is the backend's; the
    // walking is the same whoever does it.
    size_t length = text.length();
    while (length > 0) {
        do {
            --length;
        } while (length > 0 && (text[length] & 0xC0) == 0x80);
        if (!measureText(text.substr(0, length), fontSize, bold, &w, nullptr)) {
            return 0;
        }
        if (w <= maxWidth) {
            break;
        }
    }
    return length;
}

Bitmap renderText(const std::string &text, float fontSize, bool bold) {
    if (text.empty()) {
        return Bitmap();
    }
    return TextBackend::render(text, fontSize, bold);
}

void blendOver(Bitmap &dst, const Bitmap &src, int dstX, int dstY, float r, float g, float b, float a) {
    if (!dst.valid() || !src.valid()) {
        return;
    }
    // Each side read in its own order, so the tint lands on the channel it
    // names.
    const int srcRed = src.redOffset();
    const int srcBlue = src.blueOffset();
    const int dstRed = dst.redOffset();
    const int dstBlue = dst.blueOffset();
    for (int y = 0; y < src.height(); ++y) {
        int ty = dstY + y;
        if (ty < 0 || ty >= dst.height()) {
            continue;
        }
        const uint8_t *srcRow = src.pixels() + (size_t)y * (size_t)src.width() * 4;
        uint8_t *dstRow = dst.pixels() + (size_t)ty * (size_t)dst.width() * 4;
        for (int x = 0; x < src.width(); ++x) {
            int tx = dstX + x;
            if (tx < 0 || tx >= dst.width()) {
                continue;
            }
            const uint8_t *s = srcRow + (size_t)x * 4;
            uint8_t *d = dstRow + (size_t)tx * 4;
            // An a above 1 strengthens a faint source, such as a blurred halo,
            // and stops at full coverage.
            float sa = std::min(1.0f, (s[3] / 255.0f) * a);
            if (sa <= 0.0f) {
                continue;
            }
            // Include source coverage in the tint: GL_ONE blending requires premultiplied
            // colour.
            float sr = (s[srcRed] / 255.0f) * r * sa;
            float sg = (s[1] / 255.0f) * g * sa;
            float sb = (s[srcBlue] / 255.0f) * b * sa;
            float inv = 1.0f - sa;
            d[dstRed] = (uint8_t)std::min(255.0f, sr * 255.0f + d[dstRed] * inv);
            d[1] = (uint8_t)std::min(255.0f, sg * 255.0f + d[1] * inv);
            d[dstBlue] = (uint8_t)std::min(255.0f, sb * 255.0f + d[dstBlue] * inv);
            d[3] = (uint8_t)std::min(255.0f, sa * 255.0f + d[3] * inv);
        }
    }
}

void blit(Bitmap &dst, const Bitmap &src, int dstX, int dstY, float alpha) {
    if (!dst.valid() || !src.valid() || alpha <= 0.0f) {
        return;
    }
    const ChannelMap map = channelMap(src, dst);
    for (int y = 0; y < src.height(); ++y) {
        int ty = dstY + y;
        if (ty < 0 || ty >= dst.height()) {
            continue;
        }
        const uint8_t *srcRow = src.pixels() + (size_t)y * (size_t)src.width() * 4;
        uint8_t *dstRow = dst.pixels() + (size_t)ty * (size_t)dst.width() * 4;
        for (int x = 0; x < src.width(); ++x) {
            int tx = dstX + x;
            if (tx < 0 || tx >= dst.width()) {
                continue;
            }
            const uint8_t *s = srcRow + (size_t)x * 4;
            uint8_t *d = dstRow + (size_t)tx * 4;
            // Source is already premultiplied, so scaling every channel by
            // alpha keeps it that way.
            float sa = (s[3] / 255.0f) * alpha;
            if (sa <= 0.0f) {
                continue;
            }
            float inv = 1.0f - sa;
            for (int c = 0; c < 4; ++c) {
                d[map.to[c]] = (uint8_t)std::min(255.0f, s[c] * alpha + d[map.to[c]] * inv);
            }
        }
    }
}

void stamp(Bitmap &dst, const Bitmap &src, int dstX, int dstY) {
    if (!dst.valid() || !src.valid()) {
        return;
    }
    const ChannelMap map = channelMap(src, dst);
    for (int y = 0; y < src.height(); ++y) {
        int ty = dstY + y;
        if (ty < 0 || ty >= dst.height()) {
            continue;
        }
        const uint8_t *srcRow = src.pixels() + (size_t)y * (size_t)src.width() * 4;
        uint8_t *dstRow = dst.pixels() + (size_t)ty * (size_t)dst.width() * 4;
        for (int x = 0; x < src.width(); ++x) {
            int tx = dstX + x;
            if (tx < 0 || tx >= dst.width()) {
                continue;
            }
            const uint8_t *s = srcRow + (size_t)x * 4;
            uint8_t *d = dstRow + (size_t)tx * 4;
            for (int c = 0; c < 4; ++c) {
                d[map.to[c]] = s[c];
            }
        }
    }
}

void blitScaled(Bitmap &dst, const Bitmap &src, int dstX, int dstY, int width, int height, float alpha) {
    if (!dst.valid() || !src.valid() || width <= 0 || height <= 0 || alpha <= 0.0f) {
        return;
    }
    const ChannelMap map = channelMap(src, dst);
    // Bilinear interpolation in premultiplied space prevents transparent pixels contributing
    // colour.
    float scaleX = (float)src.width() / (float)width;
    float scaleY = (float)src.height() / (float)height;
    for (int y = 0; y < height; ++y) {
        int ty = dstY + y;
        if (ty < 0 || ty >= dst.height()) {
            continue;
        }
        // Sample from pixel centres, so the result is not shifted half a pixel.
        float fy = ((float)y + 0.5f) * scaleY - 0.5f;
        int y0 = (int)std::floor(fy);
        float wy = fy - (float)y0;
        int y1 = std::min(y0 + 1, src.height() - 1);
        y0 = std::min(std::max(y0, 0), src.height() - 1);
        y1 = std::max(y1, 0);
        const uint8_t *row0 = src.pixels() + (size_t)y0 * (size_t)src.width() * 4;
        const uint8_t *row1 = src.pixels() + (size_t)y1 * (size_t)src.width() * 4;
        uint8_t *dstRow = dst.pixels() + (size_t)ty * (size_t)dst.width() * 4;
        for (int x = 0; x < width; ++x) {
            int tx = dstX + x;
            if (tx < 0 || tx >= dst.width()) {
                continue;
            }
            float fx = ((float)x + 0.5f) * scaleX - 0.5f;
            int x0 = (int)std::floor(fx);
            float wx = fx - (float)x0;
            int x1 = std::min(x0 + 1, src.width() - 1);
            x0 = std::min(std::max(x0, 0), src.width() - 1);
            x1 = std::max(x1, 0);

            const uint8_t *s00 = row0 + (size_t)x0 * 4;
            const uint8_t *s01 = row0 + (size_t)x1 * 4;
            const uint8_t *s10 = row1 + (size_t)x0 * 4;
            const uint8_t *s11 = row1 + (size_t)x1 * 4;
            float sample[4];
            for (int c = 0; c < 4; ++c) {
                float top = s00[c] + (s01[c] - s00[c]) * wx;
                float bottom = s10[c] + (s11[c] - s10[c]) * wx;
                sample[c] = top + (bottom - top) * wy;
            }

            uint8_t *d = dstRow + (size_t)tx * 4;
            float sa = (sample[3] / 255.0f) * alpha;
            if (sa <= 0.0f) {
                continue;
            }
            float inv = 1.0f - sa;
            for (int c = 0; c < 4; ++c) {
                d[map.to[c]] = (uint8_t)std::min(255.0f, sample[c] * alpha + d[map.to[c]] * inv);
            }
        }
    }
}

// Copies a bitmap rectangle for nine-patch rendering.
static Bitmap subImage(const Bitmap &src, int x, int y, int width, int height) {
    if (!src.valid() || width <= 0 || height <= 0) {
        return Bitmap();
    }
    Bitmap out(width, height, src.order());
    for (int row = 0; row < height; ++row) {
        int sy = y + row;
        if (sy < 0 || sy >= src.height()) {
            continue;
        }
        for (int col = 0; col < width; ++col) {
            int sx = x + col;
            if (sx < 0 || sx >= src.width()) {
                continue;
            }
            const uint8_t *s = src.pixels() + ((size_t)sy * (size_t)src.width() + (size_t)sx) * 4;
            uint8_t *d = out.pixels() + ((size_t)row * (size_t)width + (size_t)col) * 4;
            for (int c = 0; c < 4; ++c) {
                d[c] = s[c];
            }
        }
    }
    return out;
}

// Reads one edge of the guide border and returns the run of marked pixels.
static void readGuide(const Bitmap &raw, bool horizontal, int *begin, int *end) {
    int length = horizontal ? raw.width() : raw.height();
    int first = -1;
    int last = -1;
    for (int i = 1; i < length - 1; ++i) {
        int x = horizontal ? i : 0;
        int y = horizontal ? 0 : i;
        uint8_t alpha = raw.pixels()[((size_t)y * (size_t)raw.width() + (size_t)x) * 4 + 3];
        if (alpha == 0) {
            continue;
        }
        if (first < 0) {
            first = i;
        }
        last = i;
    }
    if (first < 0) {
        // No guide. Stretch the middle pixel, which is what a plain image wants.
        *begin = (length - 2) / 2;
        *end = *begin + 1;
        return;
    }
    // Guide coordinates count the border, the content does not.
    *begin = first - 1;
    *end = last;
}

NinePatch loadNinePatch(const std::string &name) {
    NinePatch patch;
    DrawableLoad::NinePatchSource source = DrawableLoad::loadNinePatch(name);
    const Bitmap &raw = source.bitmap;
    if (source.hasGuides) {
        if (!raw.valid() || raw.width() < 3 || raw.height() < 3) {
            return patch;
        }
        readGuide(raw, true, &patch.stretchX0, &patch.stretchX1);
        readGuide(raw, false, &patch.stretchY0, &patch.stretchY1);
        patch.image = subImage(raw, 1, 1, raw.width() - 2, raw.height() - 2);
    } else {
        if (!raw.valid()) {
            return patch;
        }
        patch.image = raw;
        patch.stretchX0 = source.stretchX0;
        patch.stretchX1 = source.stretchX1;
        patch.stretchY0 = source.stretchY0;
        patch.stretchY1 = source.stretchY1;
    }

    float factor = App::UI_DENSITY / source.density;
    if (factor > 0.99f && factor < 1.01f) {
        return patch;
    }
    // The guides move with the art. Rounding them the same way the image is
    // resized keeps the stretched middle where it was.
    int width = std::max(3, (int)((float)patch.image.width() * factor + 0.5f));
    int height = std::max(3, (int)((float)patch.image.height() * factor + 0.5f));
    float scaleX = (float)width / (float)patch.image.width();
    float scaleY = (float)height / (float)patch.image.height();
    patch.stretchX0 = (int)((float)patch.stretchX0 * scaleX + 0.5f);
    patch.stretchX1 = std::max(patch.stretchX0 + 1, (int)((float)patch.stretchX1 * scaleX + 0.5f));
    patch.stretchY0 = (int)((float)patch.stretchY0 * scaleY + 0.5f);
    patch.stretchY1 = std::max(patch.stretchY0 + 1, (int)((float)patch.stretchY1 * scaleY + 0.5f));
    patch.image = patch.image.scaled(width, height);
    return patch;
}

void blitNinePatch(Bitmap &dst, const NinePatch &patch, int x, int y, int width, int height, float alpha) {
    if (!dst.valid() || !patch.valid() || width <= 0 || height <= 0 || alpha <= 0.0f) {
        return;
    }
    const Bitmap &src = patch.image;
    int capLeft = patch.stretchX0;
    int capRight = src.width() - patch.stretchX1;
    int capTop = patch.stretchY0;
    int capBottom = src.height() - patch.stretchY1;
    // Below the caps there is no room for the middle. Give each cap its share
    // of what there is instead of letting the middle go negative.
    if (capLeft + capRight > width) {
        int total = capLeft + capRight;
        capLeft = capLeft * width / total;
        capRight = width - capLeft;
    }
    if (capTop + capBottom > height) {
        int total = capTop + capBottom;
        capTop = capTop * height / total;
        capBottom = height - capTop;
    }
    int midW = width - capLeft - capRight;
    int midH = height - capTop - capBottom;

    // Source rows and columns, then the destination rows and columns they map
    // to. Index 1 is the stretched middle in both.
    int srcX[3] = {0, patch.stretchX0, patch.stretchX1};
    int srcW[3] = {patch.stretchX0, patch.stretchX1 - patch.stretchX0, src.width() - patch.stretchX1};
    int srcY[3] = {0, patch.stretchY0, patch.stretchY1};
    int srcH[3] = {patch.stretchY0, patch.stretchY1 - patch.stretchY0, src.height() - patch.stretchY1};
    int dstX[3] = {x, x + capLeft, x + capLeft + midW};
    int dstW[3] = {capLeft, midW, capRight};
    int dstY[3] = {y, y + capTop, y + capTop + midH};
    int dstH[3] = {capTop, midH, capBottom};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (srcW[col] <= 0 || srcH[row] <= 0 || dstW[col] <= 0 || dstH[row] <= 0) {
                continue;
            }
            Bitmap piece = subImage(src, srcX[col], srcY[row], srcW[col], srcH[row]);
            blitScaled(dst, piece, dstX[col], dstY[row], dstW[col], dstH[row], alpha);
        }
    }
}

Bitmap blurredCoverage(const Bitmap &src, int radius) {
    if (!src.valid() || radius <= 0) {
        return Bitmap();
    }
    const int pad = blurPadding(radius);
    const int width = src.width() + pad * 2;
    const int height = src.height() + pad * 2;

    std::vector<float> coverage((size_t)width * (size_t)height, 0.0f);
    for (int y = 0; y < src.height(); ++y) {
        const uint8_t *row = src.pixels() + (size_t)y * (size_t)src.width() * 4;
        for (int x = 0; x < src.width(); ++x) {
            coverage[(size_t)(y + pad) * (size_t)width + (size_t)(x + pad)] = row[(size_t)x * 4 + 3] / 255.0f;
        }
    }

    std::vector<float> scratch(coverage.size(), 0.0f);
    const float norm = 1.0f / (float)(radius * 2 + 1);
    // Two box passes approximate a tent filter, which is close enough to the
    // blur the original asked Paint for.
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    int sx = x + k;
                    if (sx >= 0 && sx < width) {
                        sum += coverage[(size_t)y * (size_t)width + (size_t)sx];
                    }
                }
                scratch[(size_t)y * (size_t)width + (size_t)x] = sum * norm;
            }
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    int sy = y + k;
                    if (sy >= 0 && sy < height) {
                        sum += scratch[(size_t)sy * (size_t)width + (size_t)x];
                    }
                }
                coverage[(size_t)y * (size_t)width + (size_t)x] = sum * norm;
            }
        }
    }

    Bitmap result(width, height);
    uint8_t *pixels = result.pixels();
    for (size_t i = 0; i < coverage.size(); ++i) {
        float value = std::min(1.0f, coverage[i]);
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = (uint8_t)(value * 255.0f + 0.5f);
    }
    return result;
}

void drawText(Bitmap &dst, const std::string &text, int x, int y, float fontSize, bool bold, float r, float g,
              float b, float a, int shadowRadius, float shadowAlpha) {
    Bitmap glyphs = renderText(text, fontSize, bold);
    if (!glyphs.valid()) {
        return;
    }
    if (shadowRadius > 0) {
        Bitmap shadow = blurredCoverage(glyphs, shadowRadius);
        if (shadow.valid()) {
            const int pad = blurPadding(shadowRadius);
            blendOver(dst, shadow, x - pad, y - pad, 0.0f, 0.0f, 0.0f, shadowAlpha);
        }
    }
    blendOver(dst, glyphs, x, y, r, g, b, a);
}

void drawLine(Bitmap &dst, float x0, float y0, float x1, float y1, float thickness, float r, float g, float b,
              float a) {
    if (!dst.valid() || a <= 0.0f || thickness <= 0.0f) {
        return;
    }
    // Use distance-to-segment coverage for antialiased lines.
    const float half = thickness * 0.5f;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float lengthSquared = dx * dx + dy * dy;

    const int minX = std::max(0, (int)std::floor(std::min(x0, x1) - half - 1.0f));
    const int maxX = std::min(dst.width() - 1, (int)std::ceil(std::max(x0, x1) + half + 1.0f));
    const int minY = std::max(0, (int)std::floor(std::min(y0, y1) - half - 1.0f));
    const int maxY = std::min(dst.height() - 1, (int)std::ceil(std::max(y0, y1) + half + 1.0f));

    const int red = dst.redOffset();
    const int blue = dst.blueOffset();
    for (int py = minY; py <= maxY; ++py) {
        uint8_t *row = dst.pixels() + (size_t)py * (size_t)dst.width() * 4;
        for (int px = minX; px <= maxX; ++px) {
            const float sampleX = (float)px + 0.5f;
            const float sampleY = (float)py + 0.5f;
            float t = 0.0f;
            if (lengthSquared > 0.0f) {
                t = ((sampleX - x0) * dx + (sampleY - y0) * dy) / lengthSquared;
                t = std::max(0.0f, std::min(1.0f, t));
            }
            const float nearestX = x0 + t * dx;
            const float nearestY = y0 + t * dy;
            const float distance = std::sqrt((sampleX - nearestX) * (sampleX - nearestX) +
                                             (sampleY - nearestY) * (sampleY - nearestY));
            // One pixel of falloff at the edge, which is as much as a glyph
            // this small can use.
            float coverage = half + 0.5f - distance;
            coverage = std::max(0.0f, std::min(1.0f, coverage));
            if (coverage <= 0.0f) {
                continue;
            }
            const float alpha = a * coverage;
            const float inv = 1.0f - alpha;
            uint8_t *d = row + (size_t)px * 4;
            d[red] = (uint8_t)std::min(255.0f, r * alpha * 255.0f + d[red] * inv);
            d[1] = (uint8_t)std::min(255.0f, g * alpha * 255.0f + d[1] * inv);
            d[blue] = (uint8_t)std::min(255.0f, b * alpha * 255.0f + d[blue] * inv);
            d[3] = (uint8_t)std::min(255.0f, alpha * 255.0f + d[3] * inv);
        }
    }
}

void fillRect(Bitmap &dst, int x, int y, int width, int height, float r, float g, float b, float a) {
    if (!dst.valid() || a <= 0.0f) {
        return;
    }
    float inv = 1.0f - a;
    const int red = dst.redOffset();
    const int blue = dst.blueOffset();
    for (int py = y; py < y + height; ++py) {
        if (py < 0 || py >= dst.height()) {
            continue;
        }
        uint8_t *row = dst.pixels() + (size_t)py * (size_t)dst.width() * 4;
        for (int px = x; px < x + width; ++px) {
            if (px < 0 || px >= dst.width()) {
                continue;
            }
            uint8_t *d = row + (size_t)px * 4;
            d[red] = (uint8_t)std::min(255.0f, r * a * 255.0f + d[red] * inv);
            d[1] = (uint8_t)std::min(255.0f, g * a * 255.0f + d[1] * inv);
            d[blue] = (uint8_t)std::min(255.0f, b * a * 255.0f + d[blue] * inv);
            d[3] = (uint8_t)std::min(255.0f, a * 255.0f + d[3] * inv);
        }
    }
}

}  // namespace Canvas
