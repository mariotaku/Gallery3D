#include "graphics/SubsampledDecode.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <csetjmp>
#include <vector>

#include <jpeglib.h>

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
    if (bytes == nullptr || size == 0 || maxEdge <= 0) {
        return Bitmap();
    }

    jpeg_decompress_struct cinfo {};
    JumpOnError error {};
    cinfo.err = jpeg_std_error(&error.base);
    error.base.error_exit = jumpOnFatalError;

    // Above the jump target, so a file libjpeg rejects does not leak these.
    Bitmap decoded;
    std::vector<uint8_t> row;

    if (setjmp(error.escape) == 0) {
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, (const unsigned char *)bytes, (unsigned long)size);
        if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
            // libjpeg scales by eighths. Take the smallest that still covers
            // the size asked for, so the caller's last step only ever shrinks.
            const long longest = (long)std::max(cinfo.image_width, cinfo.image_height);
            unsigned numerator = 8;
            for (long candidate = 1; candidate <= 8; ++candidate) {
                if (longest * candidate >= (long)maxEdge * 8) {
                    numerator = (unsigned)candidate;
                    break;
                }
            }
            cinfo.scale_num = numerator;
            cinfo.scale_denom = 8;
            cinfo.out_color_space = JCS_RGB;
            jpeg_start_decompress(&cinfo);

            const int width = (int)cinfo.output_width;
            const int height = (int)cinfo.output_height;
            decoded = Bitmap(width, height);
            if (decoded.valid()) {
                row.resize((size_t)width * 3);
                for (int line = 0; line < height; ++line) {
                    JSAMPROW rows[1] = {row.data()};
                    if (jpeg_read_scanlines(&cinfo, rows, 1) != 1) {
                        break;
                    }
                    // JPEG carries no alpha, so opaque pixels are already
                    // premultiplied.
                    const uint8_t *source = row.data();
                    uint8_t *destination = decoded.pixels() + (size_t)line * (size_t)width * 4;
                    for (int column = 0; column < width; ++column) {
                        destination[0] = source[0];
                        destination[1] = source[1];
                        destination[2] = source[2];
                        destination[3] = 255;
                        source += 3;
                        destination += 4;
                    }
                }
            }
            jpeg_abort_decompress(&cinfo);
        }
    }

    jpeg_destroy_decompress(&cinfo);
    return decoded;
}
