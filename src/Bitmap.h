// Stands in for android.graphics.Bitmap. Always 32 bit RGBA with premultiplied
// alpha, because the renderer blends with GL_ONE / GL_ONE_MINUS_SRC_ALPHA just
// as the original did.
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

    // The same, from bytes already in hand. A source that does not keep its
    // photos on this disk hands those over instead of a path, so nothing has to
    // be written out just to be read straight back.
    static Bitmap loadFromMemory(const void *bytes, size_t size, int maxEdge);

    // Copies straight (unpremultiplied) RGBA and premultiplies it, which is
    // what the rest of this draws with. For pixels that came from somewhere
    // other than SDL_image - a browser's decoder hands back straight alpha.
    static Bitmap fromStraightRGBA(const uint8_t *pixels, int width, int height);

    // Reads a file whole, without decoding it. The decoder may be somewhere
    // else entirely - a browser's - so the bytes have to be separable from the
    // decode.
    static bool readFile(const std::string &path, std::vector<uint8_t> *bytes);

    // Scales into a new bitmap. Uses SDL's linear scaler.
    Bitmap scaled(int newWidth, int newHeight) const;

    // Copies this bitmap into the top left of a larger transparent bitmap.
    //
    // clampEdges fills the padding by repeating the last row and column instead
    // of leaving it transparent. Only a texture that is mipmapped wants that:
    // the quad never samples the padding at full size, but a reduced level
    // averages across the boundary and would drag the transparency in. It is
    // wrong for anything drawn with extents of (1, 1), which samples the
    // padding on purpose.
    Bitmap paddedTo(int paddedWidth, int paddedHeight, bool clampEdges = false) const;

    // Scales to cover the given box and centre crops to it. This is what the
    // original's thumbnail cache stored, and why grid items have no letterbox.
    Bitmap coverCropped(int newWidth, int newHeight) const;

    // What one pass over a JPEG's EXIF header yields. Either field stays at its
    // default when the tag is missing or the file is not a JPEG.
    struct ExifInfo {
        float rotationDegrees = 0.0f;
        int64_t dateTakenMs = 0;
        // Signed decimal degrees, north and east positive. Both stay at zero
        // when the file carries no position, which is what isLatLongValid
        // treats as absent.
        double latitude = 0.0;
        double longitude = 0.0;
    };

    // Reads the EXIF orientation, capture date and position of a JPEG in one pass.
    static ExifInfo readExif(const std::string &path);

  private:
    int mWidth = 0;
    int mHeight = 0;
    std::vector<uint8_t> mPixels;
};
