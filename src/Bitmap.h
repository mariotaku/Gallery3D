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
    };

    // Reads the EXIF orientation and capture date of a JPEG in one pass.
    static ExifInfo readExif(const std::string &path);

  private:
    int mWidth = 0;
    int mHeight = 0;
    std::vector<uint8_t> mPixels;
};
