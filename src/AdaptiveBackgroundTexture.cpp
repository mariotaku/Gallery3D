#include "AdaptiveBackgroundTexture.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "App.h"
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
const int THUMBNAIL_MAX_X = 128;

// How much of the backdrop's right edge fades out.
//
// It has to be exactly what BackgroundLayer overlaps its copies by, or the
// joins show: too narrow and a copy ends while still opaque, too wide and the
// wash goes thin in a band.
//
// The original wrote this as a pixel index, 96 of a 128 wide thumbnail. That
// held only while every thumbnail was 128 by 96. Here the crop is as wide as
// the photo allows, so a portrait one came out 89 wide, the index fell outside
// it, and the fade was skipped entirely - leaving a hard vertical edge down the
// wall wherever a copy ended.
const float FADE_FRACTION = 0.25f;

int fadeFromFor(int width) {
    return (int)((float)width * (1.0f - FADE_FRACTION));
}

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

// How opaque this row of the output is. The fade runs along the output's rows,
// which is this pass's height because of the transpose: full at the first row
// of the fade and nothing at the last.
//
// Pass `height` as `fadeFrom` to mean no fade, which is what the first pass
// wants.
int fadeAlpha(int y, int height, int fadeFrom) {
    if (y < fadeFrom || height - 1 <= fadeFrom) {
        return MAX_COLOR_VALUE;
    }
    // The original divided by the width of the fade rather than the number of
    // steps across it, which starts a few percent down from opaque - enough to
    // leave a faint line where the copies meet.
    return (height - 1 - y) * MAX_COLOR_VALUE / (height - 1 - fadeFrom);
}

// A blur is separable, so this runs the kernel along each row and writes the
// output transposed. Run it twice and the image comes back the right way round,
// blurred on both axes. The source alpha is discarded; whichever pass carries
// the fade puts its own there.
void boxBlurFilter(const uint32_t *in, uint32_t *out, int width, int height, int fadeFrom) {
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
        const int alpha = fadeAlpha(y, height, fadeFrom);
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

// The weights of a gaussian, normalised, out to three standard deviations. Past
// that a tap carries less than a two hundredth of the centre one and cannot
// move an eight bit channel.
std::vector<float> gaussianKernel(float sigma) {
    if (sigma < 0.05f) {
        return std::vector<float>{1.0f};
    }
    const int half = (int)std::ceil(sigma * 3.0f);
    std::vector<float> kernel((size_t)(half * 2 + 1));
    const float twoSigmaSquared = 2.0f * sigma * sigma;
    float total = 0.0f;
    for (int i = -half; i <= half; ++i) {
        const float weight = std::exp(-(float)(i * i) / twoSigmaSquared);
        kernel[(size_t)(i + half)] = weight;
        total += weight;
    }
    for (float &weight : kernel) {
        weight /= total;
    }
    return kernel;
}

// The same shape as boxBlurFilter - along the rows, transposing as it writes -
// with a gaussian instead of a box.
//
// It convolves outright rather than sliding a sum along. A box can add one tap
// and drop another because all its weights are equal, and these are not. At
// this size that is a few hundred thousand multiplies for a whole backdrop,
// which does not show against the two rescales either side of it.
void gaussianBlurFilter(const uint32_t *in, uint32_t *out, int width, int height, int fadeFrom,
                        const std::vector<float> &kernel) {
    const int half = (int)(kernel.size() / 2);
    const int maxX = width - 1;
    int inPos = 0;
    for (int y = 0; y < height; ++y) {
        const int alpha = fadeAlpha(y, height, fadeFrom);
        int outPos = y;
        for (int x = 0; x < width; ++x) {
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            for (int i = -half; i <= half; ++i) {
                // Clamped at the edges, the same as the box does, so the border
                // does not fade towards a colour that is not in the photo.
                const uint32_t argb = in[inPos + FloatUtils::clamp(x + i, 0, maxX)];
                const float weight = kernel[(size_t)(i + half)];
                red += weight * (float)((argb >> 16) & 0xff);
                green += weight * (float)((argb >> 8) & 0xff);
                blue += weight * (float)(argb & 0xff);
            }
            out[outPos] = ((uint32_t)alpha << 24) | ((uint32_t)(int)(red + 0.5f) << 16) |
                          ((uint32_t)(int)(green + 0.5f) << 8) | (uint32_t)(int)(blue + 0.5f);
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
    // it. The first says no fade by passing its own height; either way its
    // alpha is thrown away, since each pass reads only colour and writes its
    // own.
    if (App::BACKDROP_BLUR == App::BACKDROP_BLUR_GAUSSIAN) {
        const std::vector<float> kernel = gaussianKernel(App::BACKDROP_BLUR_SIGMA);
        gaussianBlurFilter(in.data(), tmp.data(), cropWidth, cropHeight, cropHeight, kernel);
        gaussianBlurFilter(tmp.data(), in.data(), cropHeight, cropWidth, fadeFromFor(cropWidth), kernel);
    } else {
        boxBlurFilter(in.data(), tmp.data(), cropWidth, cropHeight, cropHeight);
        boxBlurFilter(tmp.data(), in.data(), cropHeight, cropWidth, fadeFromFor(cropWidth));
    }

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
