// Text through android.graphics.Paint.
//
// The platform knows which font it writes in and what to reach for when that
// font has no glyph for what was asked, which is the difference between a
// Japanese album name and a row of boxes. It also means the wall carries no
// font of its own and no SDL_ttf, which is the only library left in the apk
// that is not aligned for a 16 KB page.
#include "graphics/TextBackend.h"

#include <SDL3/SDL.h>
#include <android/bitmap.h>
#include <jni.h>

#include <mutex>
#include <string>

namespace {

// Looked up once, on the thread that has the app's class loader. A thread SDL
// attaches later carries only the bootstrap loader, which has never heard of
// this class, so FindClass from a loader thread would fail.
jclass gBridge = nullptr;
jmethodID gMeasure = nullptr;
jmethodID gRender = nullptr;
std::mutex sMutex;
bool sReady = false;

const char *const kBridgeClass = "me/mariotaku/gallery3d/TextBridge";

// SDL attaches the calling thread and detaches it when the thread ends, which
// is the lifetime wanted for the loader threads that draw most of the text.
JNIEnv *env() {
    return (JNIEnv *)SDL_GetAndroidJNIEnv();
}

bool failed(JNIEnv *e, const char *what) {
    if (e->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    SDL_Log("Java threw in %s", what);
    e->ExceptionDescribe();
    e->ExceptionClear();
    return true;
}

}  // namespace

namespace TextBackend {

bool init() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sReady) {
        return true;
    }
    JNIEnv *e = env();
    if (e == nullptr) {
        return false;
    }
    jclass local = e->FindClass(kBridgeClass);
    if (local == nullptr || failed(e, "FindClass(TextBridge)")) {
        SDL_Log("No TextBridge; text will be blank");
        return false;
    }
    gBridge = (jclass)e->NewGlobalRef(local);
    e->DeleteLocalRef(local);
    gMeasure = e->GetStaticMethodID(gBridge, "measure", "(Ljava/lang/String;FZ)[I");
    gRender = e->GetStaticMethodID(gBridge, "render", "(Ljava/lang/String;FZ)Landroid/graphics/Bitmap;");
    if (gMeasure == nullptr || gRender == nullptr || failed(e, "TextBridge methods")) {
        e->DeleteGlobalRef(gBridge);
        gBridge = nullptr;
        return false;
    }
    sReady = true;
    return true;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(sMutex);
    JNIEnv *e = env();
    if (e != nullptr && gBridge != nullptr) {
        e->DeleteGlobalRef(gBridge);
    }
    gBridge = nullptr;
    gMeasure = nullptr;
    gRender = nullptr;
    sReady = false;
}

bool ready() {
    std::lock_guard<std::mutex> lock(sMutex);
    return sReady;
}

bool measure(const std::string &text, float fontSize, bool bold, int *width, int *height) {
    if (fontSize <= 0.0f) {
        return false;
    }
    // Held across the call, so shutdown cannot drop the class while a loader
    // thread is inside it.
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sReady) {
        return false;
    }
    JNIEnv *e = env();
    if (e == nullptr) {
        return false;
    }
    jstring value = e->NewStringUTF(text.c_str());
    if (value == nullptr) {
        return false;
    }
    jintArray result =
        (jintArray)e->CallStaticObjectMethod(gBridge, gMeasure, value, (jfloat)fontSize, (jboolean)bold);
    e->DeleteLocalRef(value);
    if (failed(e, "TextBridge.measure") || result == nullptr) {
        return false;
    }
    jint parts[2] = {0, 0};
    e->GetIntArrayRegion(result, 0, 2, parts);
    e->DeleteLocalRef(result);
    if (width) {
        *width = (int)parts[0];
    }
    if (height) {
        *height = (int)parts[1];
    }
    return true;
}

Bitmap render(const std::string &text, float fontSize, bool bold) {
    if (fontSize <= 0.0f || text.empty()) {
        return Bitmap();
    }
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sReady) {
        return Bitmap();
    }
    JNIEnv *e = env();
    if (e == nullptr) {
        return Bitmap();
    }
    jstring value = e->NewStringUTF(text.c_str());
    if (value == nullptr) {
        return Bitmap();
    }
    jobject drawn = e->CallStaticObjectMethod(gBridge, gRender, value, (jfloat)fontSize, (jboolean)bold);
    e->DeleteLocalRef(value);
    if (failed(e, "TextBridge.render") || drawn == nullptr) {
        return Bitmap();
    }

    Bitmap glyphs;
    AndroidBitmapInfo info = {};
    void *pixels = nullptr;
    if (AndroidBitmap_getInfo(e, drawn, &info) == ANDROID_BITMAP_RESULT_SUCCESS &&
        info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 &&
        AndroidBitmap_lockPixels(e, drawn, &pixels) == ANDROID_BITMAP_RESULT_SUCCESS) {
        glyphs = Bitmap((int)info.width, (int)info.height);
        if (glyphs.valid()) {
            // White text on nothing, so the alpha is the coverage the caller
            // tints. The platform premultiplies, which leaves the colour at the
            // coverage as well, and Canvas multiplies by the coverage itself.
            // Only the alpha is kept, under straight white, as every other
            // backend hands its glyphs out.
            const uint8_t *source = (const uint8_t *)pixels;
            uint8_t *destination = glyphs.pixels();
            for (unsigned line = 0; line < info.height; ++line) {
                for (unsigned x = 0; x < info.width; ++x) {
                    uint8_t *pixel = destination + (size_t)x * 4;
                    pixel[0] = 255;
                    pixel[1] = 255;
                    pixel[2] = 255;
                    pixel[3] = source[(size_t)x * 4 + 3];
                }
                source += info.stride;
                destination += (size_t)info.width * 4;
            }
        }
        AndroidBitmap_unlockPixels(e, drawn);
    } else {
        SDL_Log("Drawn text was not in the pixel format expected");
    }

    // Done with the moment the pixels are copied. Waiting for the collector to
    // notice would hold a label's worth of memory for every label drawn.
    jclass bitmapClass = e->GetObjectClass(drawn);
    if (bitmapClass != nullptr) {
        jmethodID recycle = e->GetMethodID(bitmapClass, "recycle", "()V");
        if (recycle != nullptr) {
            e->CallVoidMethod(drawn, recycle);
            failed(e, "Bitmap.recycle");
        }
        e->DeleteLocalRef(bitmapClass);
    }
    e->DeleteLocalRef(drawn);
    return glyphs;
}

}  // namespace TextBackend
