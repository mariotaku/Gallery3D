// Android crops through android.graphics.BitmapRegionDecoder. It reaches the
// same libjpeg underneath while also covering PNG, WebP and whatever else the
// vendor's decoders handle, and it is already the object-per-image shape open()
// returns. A photo here has no path the app may read, so the source string is a
// content uri.
#include "graphics/RegionDecoder.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>

#include <android/bitmap.h>
#include <jni.h>

bool RegionDecoder::looksSupported(const std::string &mimeType) {
    // What BitmapRegionDecoder documents. A format it turns out not to handle
    // fails at open() instead, and that photo keeps its screennail.
    return mimeType == "image/jpeg" || mimeType == "image/png" || mimeType == "image/webp" ||
           mimeType == "image/heif" || mimeType == "image/heic";
}

namespace {

const char *const kBridgeClass = "me/mariotaku/gallery3d/RegionDecoderBridge";

// Taken once from the thread that runs main(). A loader thread attaches without
// the app's class loader and cannot look this up by name.
jclass gBridge = nullptr;
jmethodID gOpen = nullptr;
jmethodID gMimeType = nullptr;
jmethodID gWidth = nullptr;
jmethodID gHeight = nullptr;
jmethodID gDecodeRegion = nullptr;
jmethodID gClose = nullptr;
jmethodID gRecycle = nullptr;
// android.graphics.Bitmap.hasAlpha, which is false for a tile of a format
// without alpha.
jmethodID gHasAlpha = nullptr;

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
        if (threw(env, "size") || mWidth <= 0 || mHeight <= 0) {
            return false;
        }
        // The size a whole picture comes back at depends on its format, which
        // the platform tells from the bytes.
        jstring uriArgument = env->NewStringUTF(uri.c_str());
        jstring mimeType = (jstring)env->CallStaticObjectMethod(gBridge, gMimeType, uriArgument);
        env->DeleteLocalRef(uriArgument);
        if (!threw(env, "mimeType") && mimeType != nullptr) {
            const char *chars = env->GetStringUTFChars(mimeType, nullptr);
            if (chars != nullptr) {
                mSampling = Bitmap::samplingOfMimeType(chars);
                env->ReleaseStringUTFChars(mimeType, chars);
            }
            env->DeleteLocalRef(mimeType);
        }
        return true;
    }

  protected:
    Bitmap decode(int x, int y, int width, int height, int sampleSize, Bitmap::Size size) override;

  private:
    jobject mDecoder = nullptr;
    // BitmapRegionDecoder serializes on its own native lock, so two threads
    // decoding one photo would queue up inside it regardless. Holding the lock
    // here keeps the jobject handling around the call single threaded too.
    std::mutex mMutex;
};

Bitmap AndroidRegionDecoder::decode(int x, int y, int width, int height, int sampleSize, Bitmap::Size size) {
    // BitmapRegionDecoder's size is the contract's, which decodeRegion checks.
    (void)size;
    JNIEnv *env = jni();
    if (env == nullptr || mDecoder == nullptr) {
        return Bitmap();
    }

    std::lock_guard<std::mutex> lock(mMutex);

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
        if (decoded.valid() && env->CallBooleanMethod(tile, gHasAlpha) == JNI_FALSE && !threw(env, "hasAlpha")) {
            decoded.markOpaque();
        }
    } else {
        SDL_Log("A decoded region was not in the pixel format expected");
    }

    // The platform bitmap is finished with the moment its pixels are copied,
    // and waiting for the collector to notice would hold a tile's worth of
    // memory per region.
    env->CallStaticVoidMethod(gBridge, gRecycle, tile);
    threw(env, "recycle");
    env->DeleteLocalRef(tile);

    return decoded;
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
    gMimeType = env->GetStaticMethodID(gBridge, "mimeType", "(Ljava/lang/String;)Ljava/lang/String;");
    gWidth = env->GetStaticMethodID(gBridge, "width", "(Ljava/lang/Object;)I");
    gHeight = env->GetStaticMethodID(gBridge, "height", "(Ljava/lang/Object;)I");
    gDecodeRegion = env->GetStaticMethodID(gBridge, "decodeRegion",
                                           "(Ljava/lang/Object;IIIII)Landroid/graphics/Bitmap;");
    gClose = env->GetStaticMethodID(gBridge, "close", "(Ljava/lang/Object;)V");
    gRecycle = env->GetStaticMethodID(gBridge, "recycle", "(Landroid/graphics/Bitmap;)V");
    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    if (bitmapClass != nullptr) {
        gHasAlpha = env->GetMethodID(bitmapClass, "hasAlpha", "()Z");
        env->DeleteLocalRef(bitmapClass);
    }
    if (threw(env, "initAndroid") || gOpen == nullptr || gMimeType == nullptr || gDecodeRegion == nullptr ||
        gHasAlpha == nullptr) {
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
