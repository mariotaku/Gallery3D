#include "AdaptiveBackgroundTexture.h"

#include <cstdint>
#include <vector>

#include "FloatUtils.h"
#include "LocalDataSource.h"
#include "MediaItem.h"
#include "MediaSet.h"
#include "RenderView.h"

namespace {

const int RADIUS = 4;
const int KERNEL_SIZE = RADIUS * 2 + 1;
const int MAX_COLOR_VALUE = 255;
// The original ran a LightingColorFilter of 0xffaaaaaa over the result, which
// multiplies the colour channels and leaves alpha alone.
const int MULTIPLY_COLOR = 0xaa;
const int START_FADE_X = 96;
const int THUMBNAIL_MAX_X = 128;

// Utils.resizeBitmap: scale down so neither edge is longer than maxSize.
Bitmap resizeBitmap(const Bitmap &bitmap, int maxSize) {
    int srcWidth = bitmap.width();
    int srcHeight = bitmap.height();
    if (srcWidth > srcHeight) {
        if (srcWidth > maxSize) {
            return bitmap.scaled(maxSize, (maxSize * srcHeight) / srcWidth);
        }
    } else if (srcHeight > maxSize) {
        return bitmap.scaled((maxSize * srcWidth) / srcHeight, maxSize);
    }
    return bitmap;
}

// A box blur is separable, so this runs the kernel along each row and writes
// the output transposed. Run it twice and the image comes back the right way
// round, blurred on both axes. The source alpha is discarded; the second pass
// writes the horizontal fade instead.
void boxBlurFilter(const uint32_t *in, uint32_t *out, int width, int height, int startFadeX) {
    int inPos = 0;
    int maxX = width - 1;
    for (int y = 0; y < height; ++y) {
        // Evaluate the kernel for the first pixel in the row.
        int red = 0;
        int green = 0;
        int blue = 0;
        for (int i = -RADIUS; i <= RADIUS; ++i) {
            uint32_t argb = in[inPos + FloatUtils::clamp(i, 0, maxX)];
            red += (int)((argb >> 16) & 0xff);
            green += (int)((argb >> 8) & 0xff);
            blue += (int)(argb & 0xff);
        }
        int alpha = MAX_COLOR_VALUE;
        if (y >= startFadeX && height > startFadeX) {
            alpha = (height - y - 1) * MAX_COLOR_VALUE / (height - startFadeX);
        }
        int outPos = y;
        for (int x = 0; x < width; ++x) {
            out[outPos] = ((uint32_t)alpha << 24) | ((uint32_t)(red / KERNEL_SIZE) << 16) |
                          ((uint32_t)(green / KERNEL_SIZE) << 8) | (uint32_t)(blue / KERNEL_SIZE);
            // Slide to the next pixel: add the new rightmost, drop the former
            // leftmost.
            uint32_t prevArgb = in[inPos + FloatUtils::clamp(x - RADIUS, 0, maxX)];
            uint32_t nextArgb = in[inPos + FloatUtils::clamp(x + RADIUS + 1, 0, maxX)];
            red += (int)((nextArgb >> 16) & 0xff) - (int)((prevArgb >> 16) & 0xff);
            green += (int)((nextArgb >> 8) & 0xff) - (int)((prevArgb >> 8) & 0xff);
            blue += (int)(nextArgb & 0xff) - (int)(prevArgb & 0xff);
            outPos += height;
        }
        inPos += width;
    }
}

}  // namespace

bool AdaptiveBackgroundTexture::loadsOverNetwork() const {
    MediaSet *set = (mItem != nullptr) ? mItem->mParentMediaSet : nullptr;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    return source != nullptr && source->readsBlockOnNetwork();
}

Bitmap AdaptiveBackgroundTexture::load(RenderView *view) {
    // Never used: startLoad does the work, because a decode may answer later
    // than the call that started it. Here because the base class declares it.
    (void)view;
    return Bitmap();
}

void AdaptiveBackgroundTexture::startLoad(RenderView *view, const TexturePtr &self) {
    if (mItem == nullptr || mDestWidth <= 0 || mDestHeight <= 0) {
        view->finishLoad(self, Bitmap());
        return;
    }
    // Small on purpose. The result is blurred past recognition, so the photo it
    // comes from need only carry the colours, and at this size the disk cache
    // usually has it already.
    const int destWidth = mDestWidth;
    const int destHeight = mDestHeight;
    decodeItemPixels(mItem, THUMBNAIL_MAX_X, [view, self, destWidth, destHeight](Bitmap photo) {
        view->finishLoad(self, backdropFrom(photo, destWidth, destHeight));
    });
}

Bitmap AdaptiveBackgroundTexture::backdropFrom(const Bitmap &photo, int destWidth, int destHeight) {
    if (destWidth <= 0 || destHeight <= 0) {
        return Bitmap();
    }
    Bitmap source = resizeBitmap(photo, THUMBNAIL_MAX_X);
    if (!source.valid()) {
        return Bitmap();
    }

    // Crop the source to the aspect ratio of the destination.
    int sourceWidth = source.width();
    int sourceHeight = source.height();
    float fitX = (float)sourceWidth / (float)destWidth;
    float fitY = (float)sourceHeight / (float)destHeight;
    int cropX;
    int cropY;
    int cropWidth;
    int cropHeight;
    if (fitX < fitY) {
        // Full width, partial height.
        cropWidth = sourceWidth;
        cropHeight = (int)(destHeight * fitX);
        cropX = 0;
        cropY = (sourceHeight - cropHeight) / 2;
    } else {
        // Full height, partial or full width. The original measured this crop
        // against the destination height, which only works while the two are
        // square; use the width so a panorama crops instead of leaving the
        // right of the backdrop empty.
        cropWidth = (int)(destWidth * fitY);
        cropHeight = sourceHeight;
        cropX = (sourceWidth - cropWidth) / 2;
        cropY = 0;
    }
    if (cropWidth <= 0 || cropHeight <= 0) {
        return Bitmap();
    }
    if (cropWidth > sourceWidth) {
        cropWidth = sourceWidth;
        cropX = 0;
    }

    // Read the crop out as packed ARGB, which is what the filter walks.
    size_t numPixels = (size_t)cropWidth * (size_t)cropHeight;
    std::vector<uint32_t> in(numPixels);
    std::vector<uint32_t> tmp(numPixels);
    const uint8_t *pixels = source.pixels();
    for (int y = 0; y < cropHeight; ++y) {
        const uint8_t *row = pixels + ((size_t)(cropY + y) * (size_t)sourceWidth + (size_t)cropX) * 4;
        uint32_t *dst = in.data() + (size_t)y * (size_t)cropWidth;
        for (int x = 0; x < cropWidth; ++x) {
            dst[x] = ((uint32_t)row[x * 4 + 3] << 24) | ((uint32_t)row[x * 4] << 16) |
                     ((uint32_t)row[x * 4 + 1] << 8) | (uint32_t)row[x * 4 + 2];
        }
    }

    // Horizontal pass, then vertical, each transposing as it goes. The fade
    // belongs on the destination x axis, so it is the second pass that writes
    // it, while the first leaves every pixel opaque.
    boxBlurFilter(in.data(), tmp.data(), cropWidth, cropHeight, cropWidth);
    boxBlurFilter(tmp.data(), in.data(), cropHeight, cropWidth, START_FADE_X);

    Bitmap filtered(cropWidth, cropHeight);
    uint8_t *out = filtered.pixels();
    for (size_t i = 0; i < numPixels; ++i) {
        uint32_t argb = in[i];
        out[i * 4] = (uint8_t)(((argb >> 16) & 0xff) * MULTIPLY_COLOR / 255);
        out[i * 4 + 1] = (uint8_t)(((argb >> 8) & 0xff) * MULTIPLY_COLOR / 255);
        out[i * 4 + 2] = (uint8_t)((argb & 0xff) * MULTIPLY_COLOR / 255);
        out[i * 4 + 3] = (uint8_t)(argb >> 24);
    }

    // BackgroundLayer draws this with GL_SRC_ALPHA, so the fade stays straight
    // alpha rather than being premultiplied like the rest of the port.
    return filtered.scaled(destWidth, destHeight);
}
