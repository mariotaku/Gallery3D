#include "graphics/RegionDecoder.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <vector>

#include <jpeglib.h>

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
};

void jumpOnFatalError(j_common_ptr info) {
    char message[JMSG_LENGTH_MAX] = "";
    info->err->format_message(info, message);
    SDL_Log("Region decode failed: %s", message);
    std::longjmp(((JumpOnError *)info->err)->escape, 1);
}

// libjpeg scales by an eighth, so it covers sample sizes up to 8. Anything
// coarser decodes at an eighth and scales the rest of the way afterwards.
int scaleDenominatorFor(int sampleSize) {
    int denominator = 1;
    while (denominator < 8 && denominator * 2 <= sampleSize) {
        denominator *= 2;
    }
    return denominator;
}

// Holds the encoded file rather than an open libjpeg context. libjpeg cannot
// seek back into a scan it has already read, so every region restarts
// decompression regardless; what must not repeat is reading the file off the
// disk. Decoding from a shared read-only buffer also keeps the decode threads
// out of each other's way.
class JpegRegionDecoder : public RegionDecoder {
  public:
    bool read(const std::string &path);

    Bitmap decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) override;

  private:
    bool readSize();

    std::vector<uint8_t> mBytes;
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
    jpeg_decompress_struct cinfo {};
    JumpOnError error {};
    cinfo.err = jpeg_std_error(&error.base);
    error.base.error_exit = jumpOnFatalError;

    bool ok = false;
    if (setjmp(error.escape) == 0) {
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, mBytes.data(), (unsigned long)mBytes.size());
        if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
            mWidth = (int)cinfo.image_width;
            mHeight = (int)cinfo.image_height;
            ok = mWidth > 0 && mHeight > 0;
        }
    }
    jpeg_destroy_decompress(&cinfo);
    return ok;
}

Bitmap JpegRegionDecoder::decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) {
    if (width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0) {
        return Bitmap();
    }

    jpeg_decompress_struct cinfo {};
    JumpOnError error {};
    cinfo.err = jpeg_std_error(&error.base);
    error.base.error_exit = jumpOnFatalError;

    // Everything holding memory is declared here, before the jump target.
    // longjmp skips the destructors of anything built after it, so nothing
    // below the setjmp may own an allocation.
    Bitmap decoded;
    int decodedLeft = 0;
    int decodedWidth = 0;
    int decodedHeight = 0;

    // Anything libjpeg rejects lands back here with the structures still live,
    // so the cleanup below is the single exit path.
    if (setjmp(error.escape) == 0) {
        jpeg_create_decompress(&cinfo);
        // The buffer is only read, so several threads can aim their own
        // decompression at it at once.
        jpeg_mem_src(&cinfo, mBytes.data(), (unsigned long)mBytes.size());
        jpeg_read_header(&cinfo, TRUE);

        // The caller asks for a whole-pixel reduction, which is what its tile
        // grid is built on.
        const int denominator = scaleDenominatorFor(width / outWidth);
        cinfo.scale_num = 1;
        cinfo.scale_denom = (unsigned)denominator;
        // libjpeg-turbo's RGBA, with alpha at 255, so each row is written
        // straight into the bitmap. JPEG carries no alpha, so opaque pixels are
        // already premultiplied.
        cinfo.out_color_space = JCS_EXT_RGBA;
        jpeg_start_decompress(&cinfo);

        // The region arrives in the original's pixels; libjpeg works in the
        // reduced ones it is about to output.
        const int scaledLeft = std::min((int)cinfo.output_width, x / denominator);
        const int scaledTop = std::min((int)cinfo.output_height, y / denominator);
        const int scaledRight = std::min((int)cinfo.output_width, (x + width + denominator - 1) / denominator);
        const int scaledBottom = std::min((int)cinfo.output_height, (y + height + denominator - 1) / denominator);
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
                for (int line = 0; line < scaledHeight; ++line) {
                    JSAMPROW rows[1] = {decoded.pixels() + (size_t)line * (size_t)croppedWidth * 4};
                    if (jpeg_read_scanlines(&cinfo, rows, 1) != 1) {
                        break;
                    }
                }
                decodedLeft = insetX;
                decodedWidth = scaledRight - scaledLeft;
                decodedHeight = scaledHeight;
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
    // Scaling happens past the jump target, where allocating is safe again. A
    // sample size above eight, or an edge tile libjpeg rounded up, still needs
    // this last step to the requested size.
    if (decodedWidth == outWidth && decodedHeight == outHeight) {
        return decoded;
    }
    return decoded.scaled(outWidth, outHeight);
}

}  // namespace

RegionDecoderPtr RegionDecoder::open(const std::string &path) {
    auto decoder = std::make_shared<JpegRegionDecoder>();
    if (!decoder->read(path)) {
        return nullptr;
    }
    return decoder;
}
