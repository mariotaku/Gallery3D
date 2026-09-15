#include "graphics/DrawableLoad.h"

#include <cstdint>
#include <cstring>

#include <SDL3/SDL.h>

#include <android/bitmap.h>
#include <jni.h>

namespace {

const char *const kBridgeClass = "me/mariotaku/gallery3d/DrawableBridge";

jclass gBridge = nullptr;
jmethodID gDecodeScaled = nullptr;
jmethodID gDecodeUnscaled = nullptr;
jmethodID gNinePatchChunk = nullptr;
jmethodID gRecycle = nullptr;
// android.graphics.Bitmap.getDensity, in dots per inch.
jmethodID gGetDensity = nullptr;
// android.graphics.Bitmap.hasAlpha, which is false for art with no alpha.
jmethodID gHasAlpha = nullptr;

// The density Android draws a dp at 1x for.
const float kBaselineDpi = 160.0f;

// Res_png_9patch as Bitmap.getNinePatchChunk hands it back: four bytes of
// counts, seven 32 bit fields, then the x and y divisions, in the device's
// own byte order.
const size_t kChunkHeaderSize = 32;

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

// Calls one of the bridge's decoders. A local reference, or null.
jobject decode(JNIEnv *env, jmethodID method, const std::string &name) {
    jstring javaName = env->NewStringUTF(name.c_str());
    if (javaName == nullptr || threw(env, "decode")) {
        return nullptr;
    }
    jobject image = env->CallStaticObjectMethod(gBridge, method, javaName);
    env->DeleteLocalRef(javaName);
    if (threw(env, "decode")) {
        return nullptr;
    }
    return image;
}

// Copies an ARGB_8888 bitmap's pixels out. BitmapFactory premultiplies, and
// RGBA_8888 is laid out in this port's RGBA order, so only the stride differs.
Bitmap copyPixels(JNIEnv *env, jobject image) {
    Bitmap copied;
    AndroidBitmapInfo info {};
    void *pixels = nullptr;
    if (AndroidBitmap_getInfo(env, image, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 ||
        AndroidBitmap_lockPixels(env, image, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return copied;
    }
    copied = Bitmap((int)info.width, (int)info.height);
    if (copied.valid()) {
        const uint8_t *source = (const uint8_t *)pixels;
        uint8_t *destination = copied.pixels();
        const size_t rowBytes = (size_t)info.width * 4;
        for (unsigned line = 0; line < info.height; ++line) {
            std::memcpy(destination, source, rowBytes);
            source += info.stride;
            destination += rowBytes;
        }
    }
    AndroidBitmap_unlockPixels(env, image);
    if (copied.valid() && env->CallBooleanMethod(image, gHasAlpha) == JNI_FALSE && !threw(env, "hasAlpha")) {
        copied.markOpaque();
    }
    return copied;
}

float densityOf(JNIEnv *env, jobject image) {
    const jint dpi = env->CallIntMethod(image, gGetDensity);
    if (threw(env, "getDensity") || dpi <= 0) {
        return 1.0f;
    }
    return (float)dpi / kBaselineDpi;
}

void release(JNIEnv *env, jobject image) {
    env->CallStaticVoidMethod(gBridge, gRecycle, image);
    threw(env, "recycle");
    env->DeleteLocalRef(image);
}

int32_t readInt32(const uint8_t *data, size_t offset) {
    int32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

// Reads the first stretch range on each axis out of a compiled nine-patch
// chunk. The art ships with one on each.
bool readChunk(JNIEnv *env, jobject image, DrawableLoad::NinePatchSource *source) {
    jbyteArray chunk = (jbyteArray)env->CallStaticObjectMethod(gBridge, gNinePatchChunk, image);
    if (threw(env, "ninePatchChunk") || chunk == nullptr) {
        return false;
    }
    bool read = false;
    const jsize length = env->GetArrayLength(chunk);
    jbyte *bytes = env->GetByteArrayElements(chunk, nullptr);
    if (bytes != nullptr && (size_t)length >= kChunkHeaderSize) {
        const uint8_t *data = (const uint8_t *)bytes;
        const size_t xDivs = data[1];
        const size_t yDivs = data[2];
        const size_t yStart = kChunkHeaderSize + xDivs * 4;
        if (xDivs >= 2 && yDivs >= 2 && (size_t)length >= yStart + yDivs * 4) {
            source->stretchX0 = readInt32(data, kChunkHeaderSize);
            source->stretchX1 = readInt32(data, kChunkHeaderSize + 4);
            source->stretchY0 = readInt32(data, yStart);
            source->stretchY1 = readInt32(data, yStart + 4);
            read = source->stretchX1 > source->stretchX0 && source->stretchY1 > source->stretchY0;
        }
    }
    if (bytes != nullptr) {
        env->ReleaseByteArrayElements(chunk, bytes, JNI_ABORT);
    }
    env->DeleteLocalRef(chunk);
    return read;
}

}  // namespace

namespace DrawableLoad {

void init() {
    if (gBridge != nullptr) {
        return;
    }
    JNIEnv *env = jni();
    if (env == nullptr) {
        return;
    }
    jclass local = env->FindClass(kBridgeClass);
    if (local == nullptr || threw(env, "init")) {
        SDL_Log("No %s, so no drawables load", kBridgeClass);
        return;
    }
    jclass bridge = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    gDecodeScaled = env->GetStaticMethodID(bridge, "decodeScaled", "(Ljava/lang/String;)Landroid/graphics/Bitmap;");
    gDecodeUnscaled =
        env->GetStaticMethodID(bridge, "decodeUnscaled", "(Ljava/lang/String;)Landroid/graphics/Bitmap;");
    gNinePatchChunk = env->GetStaticMethodID(bridge, "ninePatchChunk", "(Landroid/graphics/Bitmap;)[B");
    gRecycle = env->GetStaticMethodID(bridge, "recycle", "(Landroid/graphics/Bitmap;)V");
    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    if (bitmapClass != nullptr) {
        gGetDensity = env->GetMethodID(bitmapClass, "getDensity", "()I");
        gHasAlpha = env->GetMethodID(bitmapClass, "hasAlpha", "()Z");
        env->DeleteLocalRef(bitmapClass);
    }
    if (threw(env, "init") || gDecodeScaled == nullptr || gDecodeUnscaled == nullptr ||
        gNinePatchChunk == nullptr || gRecycle == nullptr || gGetDensity == nullptr || gHasAlpha == nullptr) {
        SDL_Log("%s is not the shape expected", kBridgeClass);
        env->DeleteGlobalRef(bridge);
        return;
    }
    gBridge = bridge;
}

Result load(const std::string &name, bool scaled) {
    Result result;
    JNIEnv *env = jni();
    if (env == nullptr || gBridge == nullptr) {
        return result;
    }
    // A density bucket first, which Android scales to the display. Art only
    // the plain folder has comes back at its own size, as it does elsewhere.
    jobject image = scaled ? decode(env, gDecodeScaled, name) : nullptr;
    const bool fromBucket = image != nullptr;
    if (image == nullptr) {
        image = decode(env, gDecodeUnscaled, name);
    }
    if (image == nullptr) {
        SDL_Log("No drawable %s", name.c_str());
        return result;
    }
    result.bitmap = copyPixels(env, image);
    result.density = fromBucket ? densityOf(env, image) : 1.0f;
    release(env, image);
    return result;
}

NinePatchSource loadNinePatch(const std::string &name) {
    NinePatchSource source;
    JNIEnv *env = jni();
    if (env == nullptr || gBridge == nullptr) {
        return source;
    }
    jobject image = decode(env, gDecodeScaled, name);
    if (image == nullptr) {
        SDL_Log("No nine-patch %s", name.c_str());
        return source;
    }
    // aapt strips the guide border when it compiles the file and keeps the
    // stretch region in a chunk, which BitmapFactory scales with the pixels.
    source.bitmap = copyPixels(env, image);
    source.density = densityOf(env, image);
    source.hasGuides = false;
    if (!readChunk(env, image, &source) && source.bitmap.valid()) {
        // The middle pixel, as the desktop stretches art whose guide border
        // has no marks.
        SDL_Log("Nine-patch %s has no stretch region", name.c_str());
        source.stretchX0 = source.bitmap.width() / 2;
        source.stretchX1 = source.stretchX0 + 1;
        source.stretchY0 = source.bitmap.height() / 2;
        source.stretchY1 = source.stretchY0 + 1;
    }
    release(env, image);
    return source;
}

}  // namespace DrawableLoad
