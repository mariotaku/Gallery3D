// Stands in for android.graphics.Bitmap: premultiplied 32-bit RGBA for
// GL_ONE / GL_ONE_MINUS_SRC_ALPHA blending.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Bitmap {
  public:
    Bitmap() = default;
    Bitmap(int width, int height);

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

    // Decodes a file. Returns an invalid bitmap when the file cannot be read.
    // maxEdge scales the result down so neither edge exceeds it; pass 0 to keep
    // the natural size.
    static Bitmap load(const std::string &path, int maxEdge);

    // Decodes encoded bytes without an intermediate file.
    static Bitmap loadFromMemory(const void *bytes, size_t size, int maxEdge);

    // Copies straight RGBA and premultiplies it, including browser-decoded pixels.
    static Bitmap fromStraightRGBA(const uint8_t *pixels, int width, int height);

    // Reads encoded bytes without decoding.
    static bool readFile(const std::string &path, std::vector<uint8_t> *bytes);

    // Scales into a new bitmap. Uses SDL's linear scaler.
    Bitmap scaled(int newWidth, int newHeight) const;

    // Copies into a larger transparent bitmap. clampEdges repeats boundary pixels
    // for mipmapping to prevent transparent padding bleeding in; avoid it for (1, 1) extents.
    Bitmap paddedTo(int paddedWidth, int paddedHeight, bool clampEdges = false) const;

    // Scales to cover the box and centre-crops it.
    Bitmap coverCropped(int newWidth, int newHeight) const;

    // Whether any pixel is less than fully opaque.
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

  private:
    int mWidth = 0;
    int mHeight = 0;
    std::vector<uint8_t> mPixels;
};
