// Stands in for android.graphics.Bitmap: premultiplied 32-bit pixels for
// GL_ONE / GL_ONE_MINUS_SRC_ALPHA blending.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The order of a pixel's four bytes, 8 bits a channel and premultiplied either
// way. A decoder hands back the order it produces, and the GPU takes both, so
// a picture reaches its texture without its channels being exchanged.
enum class PixelOrder : uint8_t {
    RGBA,
    BGRA,
};

class Bitmap {
  public:
    Bitmap() = default;
    Bitmap(int width, int height, PixelOrder order = PixelOrder::RGBA);

    bool valid() const {
        return mWidth > 0 && mHeight > 0 && !mPixels.empty();
    }

    int width() const {
        return mWidth;
    }

    int height() const {
        return mHeight;
    }

    const uint8_t *pixels() const {
        return mPixels.data();
    }

    uint8_t *pixels() {
        return mPixels.data();
    }

    PixelOrder order() const {
        return mOrder;
    }

    // Where red and blue sit in each pixel. Green and alpha are at 1 and 3 in
    // both orders.
    int redOffset() const {
        return (mOrder == PixelOrder::RGBA) ? 0 : 2;
    }

    int blueOffset() const {
        return 2 - redOffset();
    }

    // Records that every pixel is fully opaque, which a decoder knows from the
    // format it decoded, so hasTransparency answers without reading the pixels.
    // Resizing, cropping and reordering keep it. Code that writes alpha into a
    // bitmap marks nothing, and its bitmap is scanned.
    void markOpaque() {
        mOpaque = true;
    }

    bool knownOpaque() const {
        return mOpaque;
    }

    // Exchanges red and blue in place when the bitmap is not in order already.
    void reorder(PixelOrder order);

    // A copy in order, for code that reads channels by position.
    Bitmap inOrder(PixelOrder order) const;

    // The order load and loadFromMemory give on this platform: BGRA from WIC,
    // RGBA elsewhere. A bitmap that decoded art is drawn into takes this order,
    // so the art is copied without exchanging channels.
    static PixelOrder decodeOrder();

    // Decodes a file. Returns an invalid bitmap when the file cannot be read.
    // maxEdge scales the result down so neither edge exceeds it; pass 0 to keep
    // the natural size. The decoder is the platform's: WIC on Windows,
    // SDL_image elsewhere.
    static Bitmap load(const std::string &path, int maxEdge);

    // Decodes encoded bytes without an intermediate file.
    static Bitmap loadFromMemory(const void *bytes, size_t size, int maxEdge);

    // Whether this build decodes files with the extension, given with its dot
    // and in any case. On Windows that is whatever codecs are installed.
    static bool decodesExtension(const std::string &extension);

    // Writes the bitmap as a PNG, with its alpha made straight again.
    bool savePng(const std::string &path) const;

    // Multiplies each colour channel by its alpha in place, for straight
    // pixels written into the bitmap.
    void premultiply();

    // Reads encoded bytes without decoding.
    static bool readFile(const std::string &path, std::vector<uint8_t> *bytes);

    // Scales into a new bitmap. Uses SDL's linear scaler.
    Bitmap scaled(int newWidth, int newHeight) const;

    // Copies into a larger transparent bitmap. clampEdges repeats boundary pixels
    // for mipmapping to prevent transparent padding bleeding in; avoid it for (1, 1) extents.
    Bitmap paddedTo(int paddedWidth, int paddedHeight, bool clampEdges = false) const;

    // A rectangle of the pixels. Invalid unless the rectangle lies inside.
    Bitmap cropped(int x, int y, int width, int height) const;

    // Scales to cover the box and centre-crops it.
    Bitmap coverCropped(int newWidth, int newHeight) const;

    // Whether any pixel is less than fully opaque. Reads every pixel of an
    // opaque bitmap unless it is marked as one.
    bool hasTransparency() const;

    // What one pass over a JPEG's header yields. Every field stays at its
    // default when the tag is missing or the file is not a JPEG.
    struct ExifInfo {
        float rotationDegrees = 0.0f;
        int64_t dateTakenMs = 0;
        // Signed decimal degrees, north and east positive. Both stay at zero
        // when the file carries no position, which is what isLatLongValid
        // treats as absent.
        double latitude = 0.0;
        double longitude = 0.0;
        // The frame's pixel size, which comes from the start-of-frame marker
        // rather than from EXIF. Both stay at zero for a file whose header did
        // not fit the first pass.
        int pixelWidth = 0;
        int pixelHeight = 0;
    };

    // Reads the orientation, capture date, position and pixel size of a JPEG in
    // one pass over its header.
    static ExifInfo readExif(const std::string &path);

    // The EXIF orientation value as a clockwise turn. Only the three plain
    // rotations; mirrored ones and anything unknown count as upright.
    static float degreesForOrientation(unsigned orientation);

  private:
    int mWidth = 0;
    int mHeight = 0;
    PixelOrder mOrder = PixelOrder::RGBA;
    bool mOpaque = false;
    std::vector<uint8_t> mPixels;
};
