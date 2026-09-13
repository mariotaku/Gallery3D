// Decodes rectangles of a large image without decoding the whole file, so a
// zoomed photo loads only the tiles on screen.
//
// A decoder is bound to one image and outlives the tiles taken from it: opening
// is what costs, and a zoom asks for dozens of regions from the same picture.
// This is the shape of Android's BitmapRegionDecoder, which is the backend
// there. Windows uses WIC, and the other desktops libjpeg-turbo, because
// SDL_image decodes whole files only.
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "graphics/Bitmap.h"

class RegionDecoder {
  public:
    virtual ~RegionDecoder() = default;

    // Reads the image's header. `source` is a file path on the desktop and a
    // content uri on Android, which is all a photo there has. Returns null when
    // this build cannot decode regions of it.
    static std::shared_ptr<RegionDecoder> open(const std::string &source);

    // Whether a region decoder is worth opening for this file, judged by mime
    // type alone. Callers ask this every frame, so it touches no disk.
    static bool looksSupported(const std::string &mimeType);

    // Finds the Java helper and holds on to it. Android only, and it has to run
    // on the thread SDL calls main() on: a loader thread attaches to the vm
    // without the app's class loader and cannot look the class up by name.
    // Does nothing anywhere else.
    static void initAndroid();

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

// Holds the decoder for the photo being zoomed. Opening one reads the image,
// and a zoom asks for dozens of tiles from the same picture, so reopening per
// tile would read it dozens of times over. One entry is enough: only one photo
// is fullscreen at a time.
class RegionDecoderCache {
  public:
    // The decoder for this source, opening it if it is not the one held.
    // Returns null when the source has no region decoder behind it.
    RegionDecoderPtr get(const std::string &source);

  private:
    std::mutex mMutex;
    std::string mSource;
    RegionDecoderPtr mDecoder;
};
