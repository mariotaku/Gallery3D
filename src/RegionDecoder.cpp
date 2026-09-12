#include "RegionDecoder.h"

#include <SDL3/SDL.h>

#include <algorithm>

bool RegionDecoder::looksSupported(const std::string &mimeType) {
#if defined(__ANDROID__)
    // What BitmapRegionDecoder documents. A format it turns out not to handle
    // fails at open() instead, and that photo keeps its screennail.
    return mimeType == "image/jpeg" || mimeType == "image/png" || mimeType == "image/webp" ||
           mimeType == "image/heif" || mimeType == "image/heic";
#else
    // libjpeg is the only region decoder the desktop links. Anything else opens
    // nothing and stays on one downscaled decode.
    return mimeType == "image/jpeg";
#endif
}

#if defined(GALLERY3D_HAVE_JPEG)

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
    std::vector<uint8_t> row;
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
        cinfo.out_color_space = JCS_RGB;
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

            const int scaledWidth = scaledRight - scaledLeft;
            decoded = Bitmap(scaledWidth, scaledHeight);
            if (decoded.valid()) {
                row.resize((size_t)croppedWidth * 3);
                for (int line = 0; line < scaledHeight; ++line) {
                    JSAMPROW rows[1] = {row.data()};
                    if (jpeg_read_scanlines(&cinfo, rows, 1) != 1) {
                        break;
                    }
                    // JPEG carries no alpha, so opaque pixels are already
                    // premultiplied.
                    const uint8_t *source = row.data() + (size_t)insetX * 3;
                    uint8_t *destination = decoded.pixels() + (size_t)line * (size_t)scaledWidth * 4;
                    for (int column = 0; column < scaledWidth; ++column) {
                        destination[0] = source[0];
                        destination[1] = source[1];
                        destination[2] = source[2];
                        destination[3] = 255;
                        source += 3;
                        destination += 4;
                    }
                }
                decodedWidth = scaledWidth;
                decodedHeight = scaledHeight;
            }
        }
        jpeg_abort_decompress(&cinfo);
    }

    jpeg_destroy_decompress(&cinfo);

    if (decodedWidth <= 0 || !decoded.valid()) {
        return Bitmap();
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

#elif defined(__ANDROID__)

// Android crops through android.graphics.BitmapRegionDecoder. It reaches the
// same libjpeg underneath while also covering PNG, WebP and whatever else the
// vendor's decoders handle, and it is already the object-per-image shape open()
// returns. A photo here has no path the app may read, so the source string is a
// content uri.
#include <android/bitmap.h>
#include <jni.h>

#include <mutex>

namespace {

const char *const kBridgeClass = "me/mariotaku/gallery3d/RegionDecoderBridge";

// Taken once from the thread that runs main(). A loader thread attaches without
// the app's class loader and cannot look this up by name.
jclass gBridge = nullptr;
jmethodID gOpen = nullptr;
jmethodID gWidth = nullptr;
jmethodID gHeight = nullptr;
jmethodID gDecodeRegion = nullptr;
jmethodID gClose = nullptr;
jmethodID gRecycle = nullptr;

JNIEnv *jni() {
    // SDL attaches the calling thread and detaches it when the thread ends.
    return (JNIEnv *)SDL_GetAndroidJNIEnv();
}

bool threw(JNIEnv *env, const char *what) {
    if (env == nullptr || env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    SDL_Log("Java threw in %s", what);
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

class AndroidRegionDecoder : public RegionDecoder {
  public:
    ~AndroidRegionDecoder() override {
        JNIEnv *env = jni();
        if (env == nullptr || mDecoder == nullptr) {
            return;
        }
        env->CallStaticVoidMethod(gBridge, gClose, mDecoder);
        threw(env, "close");
        env->DeleteGlobalRef(mDecoder);
    }

    bool open(JNIEnv *env, const std::string &uri) {
        jstring argument = env->NewStringUTF(uri.c_str());
        jobject local = env->CallStaticObjectMethod(gBridge, gOpen, argument);
        env->DeleteLocalRef(argument);
        if (threw(env, "open") || local == nullptr) {
            return false;
        }
        mDecoder = env->NewGlobalRef(local);
        env->DeleteLocalRef(local);

        mWidth = env->CallStaticIntMethod(gBridge, gWidth, mDecoder);
        mHeight = env->CallStaticIntMethod(gBridge, gHeight, mDecoder);
        return !threw(env, "size") && mWidth > 0 && mHeight > 0;
    }

    Bitmap decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) override;

  private:
    jobject mDecoder = nullptr;
    // BitmapRegionDecoder serializes on its own native lock, so two threads
    // decoding one photo would queue up inside it regardless. Holding the lock
    // here keeps the jobject handling around the call single threaded too.
    std::mutex mMutex;
};

Bitmap AndroidRegionDecoder::decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) {
    if (width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0) {
        return Bitmap();
    }
    JNIEnv *env = jni();
    if (env == nullptr || mDecoder == nullptr) {
        return Bitmap();
    }

    std::lock_guard<std::mutex> lock(mMutex);

    // The caller asks for a whole-number reduction; the platform rounds it down
    // to a power of two, which is what the tile grid works in anyway.
    const int sampleSize = std::max(1, width / outWidth);
    jobject tile = env->CallStaticObjectMethod(gBridge, gDecodeRegion, mDecoder, (jint)x, (jint)y,
                                               (jint)width, (jint)height, (jint)sampleSize);
    if (threw(env, "decodeRegion") || tile == nullptr) {
        return Bitmap();
    }

    Bitmap decoded;
    AndroidBitmapInfo info {};
    void *pixels = nullptr;
    if (AndroidBitmap_getInfo(env, tile, &info) == ANDROID_BITMAP_RESULT_SUCCESS &&
        info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 &&
        AndroidBitmap_lockPixels(env, tile, &pixels) == ANDROID_BITMAP_RESULT_SUCCESS) {
        decoded = Bitmap((int)info.width, (int)info.height);
        if (decoded.valid()) {
            // RGBA_8888 is R, G, B, A in memory and the platform premultiplies
            // it, which is what this port's pixels already are. Only the stride
            // differs, so the copy goes row by row.
            const uint8_t *source = (const uint8_t *)pixels;
            uint8_t *destination = decoded.pixels();
            const size_t row = (size_t)info.width * 4;
            for (unsigned line = 0; line < info.height; ++line) {
                SDL_memcpy(destination, source, row);
                source += info.stride;
                destination += row;
            }
        }
        AndroidBitmap_unlockPixels(env, tile);
    } else {
        SDL_Log("A decoded region was not in the pixel format expected");
    }

    // The platform bitmap is finished with the moment its pixels are copied,
    // and waiting for the collector to notice would hold a tile's worth of
    // memory per region.
    env->CallStaticVoidMethod(gBridge, gRecycle, tile);
    threw(env, "recycle");
    env->DeleteLocalRef(tile);

    if (!decoded.valid()) {
        return Bitmap();
    }
    if (decoded.width() == outWidth && decoded.height() == outHeight) {
        return decoded;
    }
    // An edge tile the platform rounded differently still needs the last step
    // to the size the caller asked for.
    return decoded.scaled(outWidth, outHeight);
}

}  // namespace

void RegionDecoder::initAndroid() {
    if (gBridge != nullptr) {
        return;
    }
    JNIEnv *env = jni();
    if (env == nullptr) {
        return;
    }
    jclass local = env->FindClass(kBridgeClass);
    if (local == nullptr || threw(env, "initAndroid")) {
        SDL_Log("Could not find %s, so zoomed photos stay on their screennail", kBridgeClass);
        return;
    }
    gBridge = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);

    gOpen = env->GetStaticMethodID(gBridge, "open", "(Ljava/lang/String;)Ljava/lang/Object;");
    gWidth = env->GetStaticMethodID(gBridge, "width", "(Ljava/lang/Object;)I");
    gHeight = env->GetStaticMethodID(gBridge, "height", "(Ljava/lang/Object;)I");
    gDecodeRegion = env->GetStaticMethodID(gBridge, "decodeRegion",
                                           "(Ljava/lang/Object;IIIII)Landroid/graphics/Bitmap;");
    gClose = env->GetStaticMethodID(gBridge, "close", "(Ljava/lang/Object;)V");
    gRecycle = env->GetStaticMethodID(gBridge, "recycle", "(Landroid/graphics/Bitmap;)V");
    if (threw(env, "initAndroid") || gOpen == nullptr || gDecodeRegion == nullptr) {
        SDL_Log("The region decoder bridge is not the shape expected");
        gBridge = nullptr;
    }
}

RegionDecoderPtr RegionDecoder::open(const std::string &source) {
    JNIEnv *env = jni();
    if (env == nullptr || gBridge == nullptr || source.empty()) {
        return nullptr;
    }
    auto decoder = std::make_shared<AndroidRegionDecoder>();
    if (!decoder->open(env, source)) {
        return nullptr;
    }
    return decoder;
}

#else

// The browser decodes whole images only, so a zoomed local photo stays on its
// screennail. The museum crops on the server and does not come through here.
RegionDecoderPtr RegionDecoder::open(const std::string &source) {
    (void)source;
    return nullptr;
}

#endif

#if !defined(__ANDROID__)
void RegionDecoder::initAndroid() {
}
#endif

RegionDecoderPtr RegionDecoderCache::get(const std::string &source) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mSource != source) {
        // Held across the open so that decode threads starting on the same
        // photo together read the image once between them rather than each.
        mDecoder = RegionDecoder::open(source);
        mSource = source;
    }
    return mDecoder;
}
