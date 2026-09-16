#include "graphics/SubsampledDecode.h"

#include "graphics/ColorProfile.h"
#include "graphics/EmbeddedProfile.h"

#include <SDL3/SDL.h>

#include <android/bitmap.h>
#include <jni.h>

namespace {

const char *const kBridgeClass = "me/mariotaku/gallery3d/ImageDecodeBridge";

jclass gBridge = nullptr;
jmethodID gDecode = nullptr;
jmethodID gRecycle = nullptr;
// android.graphics.Bitmap.hasAlpha, which is false for a picture decoded from
// a format without alpha.
jmethodID gHasAlpha = nullptr;

JNIEnv *jni() {
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

}  // namespace

void SubsampledDecode::init() {
    if (gBridge != nullptr) {
        return;
    }
    JNIEnv *env = jni();
    if (env == nullptr) {
        return;
    }
    jclass local = env->FindClass(kBridgeClass);
    if (local == nullptr || threw(env, "init")) {
        SDL_Log("No %s, so photos decode at full size first", kBridgeClass);
        return;
    }
    gBridge = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    gDecode = env->GetStaticMethodID(gBridge, "decodeSampled", "([BIZ)Landroid/graphics/Bitmap;");
    gRecycle = env->GetStaticMethodID(gBridge, "recycle", "(Landroid/graphics/Bitmap;)V");
    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    if (bitmapClass != nullptr) {
        gHasAlpha = env->GetMethodID(bitmapClass, "hasAlpha", "()Z");
        env->DeleteLocalRef(bitmapClass);
    }
    if (threw(env, "init") || gDecode == nullptr || gRecycle == nullptr || gHasAlpha == nullptr) {
        SDL_Log("%s is not the shape expected", kBridgeClass);
        env->DeleteGlobalRef(gBridge);
        gBridge = nullptr;
    }
}

Bitmap SubsampledDecode::decode(const void *bytes, size_t size, int maxEdge, SampleFit fit) {
    // A maxEdge of 0 or below is the whole picture, which goes through the
    // platform too, so it is colour converted as a reduced decode is.
    if (bytes == nullptr || size == 0) {
        return Bitmap();
    }
    JNIEnv *env = jni();
    if (env == nullptr || gBridge == nullptr) {
        return Bitmap();
    }

    jbyteArray encoded = env->NewByteArray((jsize)size);
    if (encoded == nullptr || threw(env, "decodeSampled")) {
        return Bitmap();
    }
    env->SetByteArrayRegion(encoded, 0, (jsize)size, (const jbyte *)bytes);
    jobject image = env->CallStaticObjectMethod(gBridge, gDecode, encoded, (jint)maxEdge,
                                                fit == SampleFit::Under ? JNI_TRUE : JNI_FALSE);
    env->DeleteLocalRef(encoded);
    if (threw(env, "decodeSampled") || image == nullptr) {
        return Bitmap();
    }

    Bitmap decoded;
    AndroidBitmapInfo info {};
    void *pixels = nullptr;
    if (AndroidBitmap_getInfo(env, image, &info) == ANDROID_BITMAP_RESULT_SUCCESS &&
        info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 &&
        AndroidBitmap_lockPixels(env, image, &pixels) == ANDROID_BITMAP_RESULT_SUCCESS) {
        decoded = Bitmap((int)info.width, (int)info.height);
        if (decoded.valid()) {
            // RGBA_8888 is premultiplied and byte ordered the same as this
            // port's pixels, so only the stride differs.
            const uint8_t *source = (const uint8_t *)pixels;
            uint8_t *destination = decoded.pixels();
            const size_t rowBytes = (size_t)info.width * 4;
            for (unsigned line = 0; line < info.height; ++line) {
                SDL_memcpy(destination, source, rowBytes);
                source += info.stride;
                destination += rowBytes;
            }
        }
        AndroidBitmap_unlockPixels(env, image);
        // BitmapFactory converts an embedded profile, but not Adobe RGB named
        // by EXIF alone, which every other platform converts. Only JPEG carries
        // the tag, and a JPEG is opaque, so the pixels are straight.
        if (decoded.valid() && Bitmap::readExif(bytes, size).colorSpace == 2 &&
            EmbeddedProfile::of(bytes, size).empty()) {
            ColorProfile::adobeRgbToSrgb(decoded.pixels(), (size_t)decoded.width() * (size_t)decoded.height());
        }
        if (decoded.valid()) {
            if (env->CallBooleanMethod(image, gHasAlpha) == JNI_FALSE && !threw(env, "hasAlpha")) {
                decoded.markOpaque();
            } else {
                decoded.markOpaqueUnlessTransparent();
            }
        }
    }

    env->CallStaticVoidMethod(gBridge, gRecycle, image);
    threw(env, "recycle");
    env->DeleteLocalRef(image);
    return decoded;
}
