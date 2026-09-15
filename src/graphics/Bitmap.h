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

// How a format's decode is reduced by a sample size. Every platform follows
// Android's decoders (Skia's SkAndroidCodec on Android 14), so a picture comes
// out the same size, and nearly the same pixels, everywhere.
enum class Sampling : uint8_t {
    // JPEG: the codec divides by 2, 4 or 8 and rounds up, averaging each
    // block. A sample past 8 is then picked from that, as Picked picks.
    Jpeg,
    // WebP: rescaled to the size divided and rounded to the nearest pixel.
    Rescaled,
    // Everything else: divided and rounded down, keeping one pixel of every
    // sample, the one at half the sample into it.
    Picked,
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
    // The size is sampledSize for sampleSizeFor(width, height, maxEdge). The decoder is the
    // platform's: WIC on Windows, SDL_image elsewhere. tests/test_decode.cpp
    // states the whole contract and checks it against shared fixtures.
    static Bitmap load(const std::string &path, int maxEdge);

    // Decodes encoded bytes without an intermediate file.
    static Bitmap loadFromMemory(const void *bytes, size_t size, int maxEdge);

    // Whether encoded bytes are a PNG or a JPEG that is cut short: a PNG with
    // no IEND chunk near its end, or a JPEG with no end-of-image marker after
    // its first scan. Some decoders hand out what they could read of such a
    // file, so every decoder checks this first. Other formats answer false.
    static bool endsEarly(const void *bytes, size_t size);

    // Whether this build decodes files with the extension, given with its dot
    // and in any case. On Windows that is whatever codecs are installed.
    static bool decodesExtension(const std::string &extension);

    // Whether a WebP decode also reads lossless (VP8L) files. True wherever
    // WebP decodes at all, except on Windows with an older WebP codec, such
    // as Windows Server 2022's, which reads lossy files only.
    static bool decodesLosslessWebp();

    // Writes the bitmap as a PNG, with its alpha made straight again.
    bool savePng(const std::string &path) const;

    // Multiplies each colour channel by its alpha in place, for straight
    // pixels written into the bitmap.
    void premultiply();

    // Reads encoded bytes without decoding.
    static bool readFile(const std::string &path, std::vector<uint8_t> *bytes);

    // Scales into a new bitmap, the same on every platform. Shrinking averages
    // the pixels each new pixel covers. Enlarging interpolates between the two
    // nearest pixel centres.
    Bitmap scaled(int newWidth, int newHeight) const;

    // Keeps one pixel in every few, as Skia's sampling decoders do: across,
    // every width / newWidth pixels, starting half that far in, and the same
    // down. Only shrinks.
    Bitmap picked(int newWidth, int newHeight) const;

    // A whole decode of a picture in the given format, reduced by sampleSize
    // the way the format's Sampling says. A JPEG's block averages are
    // approximated by an area average, for a decoder with no reduction of its
    // own.
    Bitmap sampledFromWhole(Sampling sampling, int sampleSize) const;

    // Copies into a larger transparent bitmap. clampEdges repeats boundary pixels
    // for mipmapping to prevent transparent padding bleeding in; avoid it for (1, 1) extents.
    Bitmap paddedTo(int paddedWidth, int paddedHeight, bool clampEdges = false) const;

    // A rectangle of the pixels. Invalid unless the rectangle lies inside.
    Bitmap cropped(int x, int y, int width, int height) const;

    // A picture shown upright for an EXIF orientation, 1 to 8, turned and
    // flipped back to how its pixels are stored. A thumbnail store hands its
    // thumbnails out upright, and a decode hands pixels out as stored. Any
    // other orientation leaves the picture as it is.
    Bitmap toStoredOrientation(int orientation) const;

    // Scales to cover the box and centre-crops it.
    Bitmap coverCropped(int newWidth, int newHeight) const;

    // Whether any pixel is less than fully opaque. Reads every pixel of an
    // opaque bitmap unless it is marked as one.
    bool hasTransparency() const;

    // Marks the bitmap opaque when no pixel is transparent. A decoder whose
    // output has an alpha channel calls it, so a picture that never uses its
    // alpha is known opaque the same way on every platform.
    void markOpaqueUnlessTransparent();

    struct Size {
        int width;
        int height;
    };

    // The power of two a decode for maxEdge reduces by: the largest whose
    // reduced long edge, rounded down, still reaches maxEdge. 1 for a maxEdge
    // of 0 or below, or a picture that does not reach it.
    static int sampleSizeFor(int width, int height, int maxEdge);

    // The size a whole decode reduced by sampleSize gives, for the format's
    // Sampling. A sample of 1 keeps the size.
    static Size sampledSize(Sampling sampling, int width, int height, int sampleSize);

    // The size a region decode of a rectangle other than the whole picture
    // gives: rounded down, never below 1, whatever the format.
    static Size sampledRegionSize(int width, int height, int sampleSize);

    // The Sampling of encoded bytes, told by their signature.
    static Sampling samplingOf(const void *bytes, size_t size);

    // The Sampling of a mime type, such as a media store reports.
    static Sampling samplingOfMimeType(const std::string &mimeType);

    // What one pass over a JPEG's header yields. Every field stays at its
    // default when the tag is missing or the file is not a JPEG. A camera RAW
    // in a TIFF container yields its orientation alone.
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
        // The EXIF orientation value, 1 to 8, mirrored ones included.
        int orientation = 1;
        // The EXIF ColorSpace value: 1 for sRGB, 2 for Adobe RGB as some
        // cameras write it, 0 when the tag is missing.
        int colorSpace = 0;
        // Where the thumbnail a camera stores beside the photo sits in the
        // encoded bytes, and how long it is. Both stay at zero when there is
        // none.
        size_t thumbnailOffset = 0;
        size_t thumbnailLength = 0;
    };

    // Reads the orientation, capture date, position and pixel size of a JPEG in
    // one pass over its header.
    static ExifInfo readExif(const std::string &path);
    static ExifInfo readExif(const void *bytes, size_t size);

    // Whether a picture has the shape of another, to within 2% of the other's
    // aspect ratio. A thumbnail stored beside a photo in its shape passes; one
    // cropped to a square does not.
    static bool sameShape(int width, int height, int otherWidth, int otherHeight);

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
