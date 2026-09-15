#include "graphics/SubsampledDecode.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <csetjmp>
#include <cstdlib>

#include <jpeglib.h>
// After jpeglib.h, which it depends on.
#include <jerror.h>

#include "graphics/ColorProfile.h"
#include "platform/desktop/IccToSrgb.h"

#if defined(_MSC_VER)
// 4324: jmp_buf carries an alignment that pads the struct holding it.
// 4611: setjmp beside C++ objects. Everything that owns memory below is
// declared above the jump target, so nothing leaks when libjpeg jumps.
#pragma warning(disable : 4324 4611)
#endif

namespace {

struct JumpOnError {
    jpeg_error_mgr base;
    std::jmp_buf escape;
};

void jumpOnFatalError(j_common_ptr info) {
    char message[JMSG_LENGTH_MAX] = "";
    info->err->format_message(info, message);
    SDL_Log("Subsampled decode failed: %s", message);
    std::longjmp(((JumpOnError *)info->err)->escape, 1);
}

}  // namespace

void SubsampledDecode::init() {
    // libjpeg needs nothing found at runtime.
}

Bitmap SubsampledDecode::decode(const void *bytes, size_t size, int maxEdge) {
    // Only a JPEG goes to libjpeg. Anything else would fail there too, but
    // only after it logged an error for a file that was never its to read.
    const unsigned char *start = (const unsigned char *)bytes;
    if (bytes == nullptr || size < 3 || start[0] != 0xFF || start[1] != 0xD8 || start[2] != 0xFF) {
        return Bitmap();
    }

    jpeg_decompress_struct cinfo {};
    JumpOnError error {};
    cinfo.err = jpeg_std_error(&error.base);
    error.base.error_exit = jumpOnFatalError;

    // Above the jump target, so a file libjpeg rejects does not leak them.
    Bitmap decoded;
    IccToSrgb toSrgb;
    int originalWidth = 0;
    int originalHeight = 0;
    // A four channel JPEG, and whether an Adobe marker says its values are
    // stored inverted, as Photoshop writes them.
    bool cmyk = false;
    bool adobeInverted = false;

    if (setjmp(error.escape) == 0) {
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, (const unsigned char *)bytes, (unsigned long)size);
        // Kept, so the colour profile a camera or editor embeds can be read.
        jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);
        if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
            JOCTET *profile = nullptr;
            unsigned int profileSize = 0;
            if (jpeg_read_icc_profile(&cinfo, &profile, &profileSize)) {
                toSrgb.open(profile, profileSize);
                std::free(profile);
            }
            // libjpeg scales by eighths. Take the smallest that still covers
            // the size asked for, so the last step below only ever shrinks.
            // No maxEdge is the whole picture.
            originalWidth = (int)cinfo.image_width;
            originalHeight = (int)cinfo.image_height;
            const long longest = (long)std::max(cinfo.image_width, cinfo.image_height);
            unsigned numerator = 8;
            for (long candidate = 1; maxEdge > 0 && candidate <= 8; ++candidate) {
                if (longest * candidate >= (long)maxEdge * 8) {
                    numerator = (unsigned)candidate;
                    break;
                }
            }
            cinfo.scale_num = numerator;
            cinfo.scale_denom = 8;
            // libjpeg-turbo's RGBA, with alpha at 255, so each row is written
            // straight into the bitmap. JPEG carries no alpha, so opaque
            // pixels are already premultiplied. A four channel JPEG comes out
            // as CMYK in the same four bytes, and is converted below.
            cmyk = cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK;
            adobeInverted = cmyk && cinfo.saw_Adobe_marker;
            cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_EXT_RGBA;
            jpeg_start_decompress(&cinfo);

            const int width = (int)cinfo.output_width;
            const int height = (int)cinfo.output_height;
            decoded = Bitmap(width, height);
            if (decoded.valid()) {
                decoded.markOpaque();
                for (int line = 0; line < height; ++line) {
                    JSAMPROW rows[1] = {decoded.pixels() + (size_t)line * (size_t)width * 4};
                    if (jpeg_read_scanlines(&cinfo, rows, 1) != 1) {
                        decoded = Bitmap();
                        break;
                    }
                }
                // libjpeg pads a file that ends early with grey rows and only
                // warns. Such a picture is not the photo, so it does not decode.
                if (error.base.msg_code == JWRN_JPEG_EOF) {
                    decoded = Bitmap();
                }
            }
            jpeg_abort_decompress(&cinfo);
        }
    }

    jpeg_destroy_decompress(&cinfo);
    if (!decoded.valid()) {
        return Bitmap();
    }
    if (cmyk) {
        // Without colour management, the way every platform here converts a
        // four channel JPEG that embeds no CMYK profile.
        uint8_t *pixel = decoded.pixels();
        const uint8_t *end = pixel + (size_t)decoded.width() * (size_t)decoded.height() * 4;
        for (; pixel < end; pixel += 4) {
            // How much of each ink there is, from 0 to 255.
            const unsigned c = adobeInverted ? 255u - pixel[0] : pixel[0];
            const unsigned m = adobeInverted ? 255u - pixel[1] : pixel[1];
            const unsigned y = adobeInverted ? 255u - pixel[2] : pixel[2];
            const unsigned k = adobeInverted ? 255u - pixel[3] : pixel[3];
            pixel[0] = (uint8_t)(((255u - c) * (255u - k) + 127u) / 255u);
            pixel[1] = (uint8_t)(((255u - m) * (255u - k) + 127u) / 255u);
            pixel[2] = (uint8_t)(((255u - y) * (255u - k) + 127u) / 255u);
            pixel[3] = 255;
        }
    }
    const size_t count = (size_t)decoded.width() * (size_t)decoded.height();
    if (toSrgb) {
        toSrgb.convert(decoded.pixels(), count);
    } else if (Bitmap::readExif(bytes, size).colorSpace == 2) {
        // No profile, but EXIF says Adobe RGB, which WIC honours as well.
        ColorProfile::adobeRgbToSrgb(decoded.pixels(), count);
    }
    const Bitmap::Size fitted = Bitmap::fitWithin(originalWidth, originalHeight, maxEdge);
    return decoded.scaled(fitted.width, fitted.height);
}
