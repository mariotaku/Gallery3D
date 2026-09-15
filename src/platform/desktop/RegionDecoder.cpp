#include "graphics/RegionDecoder.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <jpeglib.h>
// After jpeglib.h, which it depends on.
#include <jerror.h>

#include "graphics/Cmyk.h"
#include "graphics/ColorProfile.h"
#include "platform/desktop/IccToSrgb.h"

#if defined(_MSC_VER)
// 4324: jmp_buf carries an alignment that pads the struct holding it.
// 4611: setjmp beside C++ objects. Both functions below keep every object that
// owns memory above the jump target, so nothing leaks when libjpeg jumps.
#pragma warning(disable : 4324 4611)
#endif

bool RegionDecoder::looksSupported(const std::string &mimeType) {
    // libjpeg is the only region decoder the desktop links. Anything else opens
    // nothing and stays on one downscaled decode.
    return mimeType == "image/jpeg";
}

void RegionDecoder::initAndroid() {
}

namespace {

// libjpeg's default error handler exits the process. Replace it with a jump
// back to the caller, which is the only way out of a C library that reports
// failure by calling error_exit.
struct JumpOnError {
    jpeg_error_mgr base;
    std::jmp_buf escape;
    // Whether libjpeg ran out of data and padded the scan, which it only warns
    // about.
    bool endedEarly;
};

void jumpOnFatalError(j_common_ptr info) {
    char message[JMSG_LENGTH_MAX] = "";
    info->err->format_message(info, message);
    SDL_Log("Region decode failed: %s", message);
    std::longjmp(((JumpOnError *)info->err)->escape, 1);
}

// Takes libjpeg's warnings and traces instead of printing them, and notes the
// one that says the file ended early.
void noteMessage(j_common_ptr info, int level) {
    if (level < 0) {
        ++info->err->num_warnings;
        if (info->err->msg_code == JWRN_JPEG_EOF) {
            ((JumpOnError *)info->err)->endedEarly = true;
        }
    }
}

// Holds the encoded file rather than an open libjpeg context. libjpeg cannot
// seek back into a scan it has already read, so every region restarts
// decompression regardless; what must not repeat is reading the file off the
// disk. Decoding from a shared read-only buffer also keeps the decode threads
// out of each other's way.
class JpegRegionDecoder : public RegionDecoder {
  public:
    JpegRegionDecoder() {
        mSampling = Sampling::Jpeg;
    }

    bool read(const std::string &path);

  protected:
    Bitmap decode(int x, int y, int width, int height, int sampleSize, Bitmap::Size size) override;

  private:
    bool readSize();

    std::vector<uint8_t> mBytes;
    // From the photo's embedded colour profile to sRGB, read once and shared
    // by every tile, so the tiles match the screennail under them.
    IccToSrgb mToSrgb;
    // No profile, but EXIF ColorSpace says Adobe RGB, as a whole decode honours.
    bool mAdobeRgb = false;
};

bool JpegRegionDecoder::read(const std::string &path) {
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return false;
    }
    mBytes.resize((size_t)size);
    const size_t got = std::fread(mBytes.data(), 1, mBytes.size(), file);
    std::fclose(file);
    return got == mBytes.size() && readSize();
}

bool JpegRegionDecoder::readSize() {
    // Only a JPEG goes to libjpeg, which would otherwise log an error for a
    // file that was never its to read. One cut short opens nothing, as it
    // decodes nothing.
    if (mBytes.size() < 3 || mBytes[0] != 0xFF || mBytes[1] != 0xD8 || mBytes[2] != 0xFF ||
        Bitmap::endsEarly(mBytes.data(), mBytes.size())) {
        return false;
    }
    jpeg_decompress_struct cinfo {};
    JumpOnError error {};
    cinfo.err = jpeg_std_error(&error.base);
    error.base.error_exit = jumpOnFatalError;
    error.base.emit_message = noteMessage;

    bool ok = false;
    if (setjmp(error.escape) == 0) {
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, mBytes.data(), (unsigned long)mBytes.size());
        jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);
        if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
            mWidth = (int)cinfo.image_width;
            mHeight = (int)cinfo.image_height;
            ok = mWidth > 0 && mHeight > 0;
            JOCTET *profile = nullptr;
            unsigned int profileSize = 0;
            if (jpeg_read_icc_profile(&cinfo, &profile, &profileSize)) {
                mToSrgb.open(profile, profileSize);
                std::free(profile);
            }
        }
    }
    jpeg_destroy_decompress(&cinfo);
    mAdobeRgb = ok && !mToSrgb && Bitmap::readExif(mBytes.data(), mBytes.size()).colorSpace == 2;
    return ok;
}

Bitmap JpegRegionDecoder::decode(int x, int y, int width, int height, int sampleSize, Bitmap::Size size) {
    jpeg_decompress_struct cinfo {};
    JumpOnError error {};
    cinfo.err = jpeg_std_error(&error.base);
    error.base.error_exit = jumpOnFatalError;
    error.base.emit_message = noteMessage;

    // Everything holding memory is declared here, before the jump target.
    // longjmp skips the destructors of anything built after it, so nothing
    // below the setjmp may own an allocation.
    Bitmap decoded;
    int decodedLeft = 0;
    int decodedWidth = 0;
    int decodedHeight = 0;
    // A four channel JPEG, and whether an Adobe marker says its values are
    // stored inverted, as Photoshop writes them.
    bool cmyk = false;
    bool inverted = false;

    // Anything libjpeg rejects lands back here with the structures still live,
    // so the cleanup below is the single exit path.
    if (setjmp(error.escape) == 0) {
        jpeg_create_decompress(&cinfo);
        // The buffer is only read, so several threads can aim their own
        // decompression at it at once.
        jpeg_mem_src(&cinfo, mBytes.data(), (unsigned long)mBytes.size());
        jpeg_read_header(&cinfo, TRUE);

        // libjpeg reduces by a half, a quarter or an eighth, rounding its size
        // up. A sample past an eighth is picked from that below, as Android's
        // decoder picks.
        const int native = std::min(sampleSize, 8);
        cinfo.scale_num = 1;
        cinfo.scale_denom = (unsigned)native;
        // libjpeg-turbo's RGBA, with alpha at 255, so each row is written
        // straight into the bitmap. JPEG carries no alpha, so opaque pixels are
        // already premultiplied. A four channel JPEG comes out as CMYK in the
        // same four bytes, and is converted below.
        cmyk = cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK;
        inverted = cmyk && cinfo.saw_Adobe_marker;
        cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_EXT_RGBA;
        jpeg_start_decompress(&cinfo);

        // The region arrives in the original's pixels; libjpeg works in the
        // reduced ones it is about to output. Skia takes the whole reduced
        // picture for the whole rectangle, and otherwise the rectangle's size
        // divided and rounded down, from its corner divided.
        const bool whole = x == 0 && y == 0 && width == mWidth && height == mHeight;
        const int scaledLeft = std::min((int)cinfo.output_width, x / native);
        const int scaledTop = std::min((int)cinfo.output_height, y / native);
        const int scaledRight =
            whole ? (int)cinfo.output_width
                  : std::min((int)cinfo.output_width, scaledLeft + (width < native ? 1 : width / native));
        const int scaledBottom =
            whole ? (int)cinfo.output_height
                  : std::min((int)cinfo.output_height, scaledTop + (height < native ? 1 : height / native));
        const int scaledHeight = scaledBottom - scaledTop;

        if (scaledRight > scaledLeft && scaledHeight > 0) {
            // jpeg_crop_scanline snaps the left edge back to an iMCU boundary
            // and widens the run to match, so it hands back a window that can
            // start left of the one asked for.
            unsigned croppedLeft = (unsigned)scaledLeft;
            unsigned croppedWidth = (unsigned)(scaledRight - scaledLeft);
            jpeg_crop_scanline(&cinfo, &croppedLeft, &croppedWidth);
            const int insetX = scaledLeft - (int)croppedLeft;

            jpeg_skip_scanlines(&cinfo, (unsigned)scaledTop);

            // The whole window libjpeg decodes, which is trimmed to the region
            // past the jump target below.
            decoded = Bitmap((int)croppedWidth, scaledHeight);
            if (decoded.valid()) {
                decoded.markOpaque();
                bool complete = true;
                for (int line = 0; line < scaledHeight && complete; ++line) {
                    JSAMPROW rows[1] = {decoded.pixels() + (size_t)line * (size_t)croppedWidth * 4};
                    complete = jpeg_read_scanlines(&cinfo, rows, 1) == 1;
                }
                // libjpeg pads a file that ends early with grey rows and only
                // warns. Such a tile is not the photo, so it does not decode.
                if (complete && !error.endedEarly) {
                    decodedLeft = insetX;
                    decodedWidth = scaledRight - scaledLeft;
                    decodedHeight = scaledHeight;
                }
            }
        }
        jpeg_abort_decompress(&cinfo);
    }

    jpeg_destroy_decompress(&cinfo);

    if (decodedWidth <= 0 || !decoded.valid()) {
        return Bitmap();
    }
    // The window can start left of the region and run wider than it, and
    // only then is it copied down to the region.
    if (decodedLeft != 0 || decoded.width() != decodedWidth) {
        decoded = decoded.cropped(decodedLeft, 0, decodedWidth, decodedHeight);
    }
    const size_t count = (size_t)decoded.width() * (size_t)decoded.height();
    if (cmyk) {
        Cmyk::toPixels(decoded.pixels(), decoded.pixels(), count, PixelOrder::RGBA, inverted);
    }
    if (mToSrgb) {
        mToSrgb.convert(decoded.pixels(), count);
    } else if (mAdobeRgb) {
        ColorProfile::adobeRgbToSrgb(decoded.pixels(), count);
    }
    // A sample past an eighth keeps one pixel of every few, past the jump
    // target, where allocating is safe again.
    return decoded.picked(size.width, size.height);
}

}  // namespace

RegionDecoderPtr RegionDecoder::open(const std::string &path) {
    auto decoder = std::make_shared<JpegRegionDecoder>();
    if (!decoder->read(path)) {
        return nullptr;
    }
    return decoder;
}
