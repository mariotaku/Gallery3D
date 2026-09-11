// Decodes rectangles of a large image without decoding the whole file, so a
// zoomed photo loads only the tiles on screen.
//
// A decoder is bound to one image and outlives the tiles taken from it: opening
// is what costs, and a zoom asks for dozens of regions from the same picture.
// This is the shape of Android's BitmapRegionDecoder, which is the backend
// there. The desktop uses libjpeg-turbo, because SDL_image decodes whole files
// only.
#pragma once

#include <memory>
#include <string>

#include "Bitmap.h"

class RegionDecoder {
  public:
    virtual ~RegionDecoder() = default;

    // Reads the file's header. Returns null when this build cannot decode
    // regions of it, which is the answer for every format but JPEG.
    static std::shared_ptr<RegionDecoder> open(const std::string &path);

    // Whether a region decoder is worth opening for this file, judged by name
    // alone. Callers ask this every frame, so it touches no disk.
    static bool looksSupported(const std::string &mimeType);

    int width() const {
        return mWidth;
    }

    int height() const {
        return mHeight;
    }

    // Decodes the rectangle given in the original's pixels and returns it at
    // outWidth by outHeight. Returns an invalid Bitmap if it cannot.
    // Several decode threads share one decoder, so this must stay callable
    // from all of them at once.
    virtual Bitmap decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) = 0;

  protected:
    int mWidth = 0;
    int mHeight = 0;
};

using RegionDecoderPtr = std::shared_ptr<RegionDecoder>;
