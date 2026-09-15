#include "platform/android/AndroidBridge.h"

#include <android/bitmap.h>
#include <jni.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <SDL3/SDL.h>

namespace {

const char *const kBridgeClass = "me/mariotaku/gallery3d/MediaStoreBridge";
const char *const kActivityClass = "me/mariotaku/gallery3d/MainActivity";
const char *const kStorageClass = "me/mariotaku/gallery3d/StorageBridge";

// A global reference to the helper class, taken once on the thread that runs
// main(). FindClass resolves against the class loader of the calling thread,
// and a thread attached later carries only the bootstrap loader, which knows
// nothing about the app's own classes.
jclass gBridge = nullptr;
jclass gActivity = nullptr;
jclass gStorage = nullptr;
Uint32 gFolderPickedEvent = 0;

// Attaches the calling thread to the vm for as long as it is in scope. The
// loader threads are made by SDL and are not attached, and detaching one that
// was already attached would pull the environment out from under the caller.
class ScopedEnv {
  public:
    ScopedEnv() {
        // SDL attaches the calling thread itself and remembers to detach it
        // when the thread ends, which is exactly the lifetime wanted here.
        mEnv = (JNIEnv *)SDL_GetAndroidJNIEnv();
    }

    JNIEnv *operator->() const {
        return mEnv;
    }

    JNIEnv *get() const {
        return mEnv;
    }

    explicit operator bool() const {
        return mEnv != nullptr;
    }

  private:
    JNIEnv *mEnv = nullptr;
};

// Logs and clears a pending exception, so the next call does not fail on a
// leftover one.
bool failed(JNIEnv *env, const char *what) {
    if (env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    SDL_Log("Java threw in %s", what);
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

std::string toString(JNIEnv *env, jstring value) {
    if (value == nullptr) {
        return std::string();
    }
    const char *utf8 = env->GetStringUTFChars(value, nullptr);
    std::string result = (utf8 != nullptr) ? utf8 : "";
    if (utf8 != nullptr) {
        env->ReleaseStringUTFChars(value, utf8);
    }
    return result;
}

// Calls a static method that takes nothing and returns a String.
std::string callStringMethod(const char *name) {
    ScopedEnv env;
    if (!env || gBridge == nullptr) {
        return std::string();
    }
    jmethodID method = env->GetStaticMethodID(gBridge, name, "()Ljava/lang/String;");
    if (method == nullptr || failed(env.get(), name)) {
        return std::string();
    }
    jstring result = (jstring)env->CallStaticObjectMethod(gBridge, method);
    std::string text;
    if (!failed(env.get(), name)) {
        text = toString(env.get(), result);
    }
    if (result != nullptr) {
        env->DeleteLocalRef(result);
    }
    return text;
}

// The answer to a permission request, which arrives on another thread.
struct PermissionAnswer {
    std::mutex mutex;
    std::condition_variable ready;
    bool answered = false;
    bool granted = false;
};

void onPermission(void *userdata, const char *permission, bool granted) {
    (void)permission;
    PermissionAnswer *answer = (PermissionAnswer *)userdata;
    {
        std::lock_guard<std::mutex> lock(answer->mutex);
        answer->granted = granted;
        answer->answered = true;
    }
    answer->ready.notify_all();
}

}  // namespace

void AndroidBridge::init() {
    if (gBridge != nullptr) {
        return;
    }
    ScopedEnv env;
    if (!env) {
        return;
    }
    jclass local = env->FindClass(kBridgeClass);
    if (local == nullptr || failed(env.get(), "init")) {
        SDL_Log("Could not find %s, so the photo library stays empty", kBridgeClass);
        return;
    }
    gBridge = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);

    jclass activity = env->FindClass(kActivityClass);
    if (activity == nullptr || failed(env.get(), "init")) {
        return;
    }
    gActivity = (jclass)env->NewGlobalRef(activity);
    env->DeleteLocalRef(activity);

    jclass storage = env->FindClass(kStorageClass);
    if (storage == nullptr || failed(env.get(), "init")) {
        SDL_Log("Could not find %s, so no other storage can be chosen", kStorageClass);
        return;
    }
    gStorage = (jclass)env->NewGlobalRef(storage);
    env->DeleteLocalRef(storage);
    gFolderPickedEvent = SDL_RegisterEvents(1);
}

bool AndroidBridge::systemBarInsets(int *left, int *top, int *right, int *bottom) {
    ScopedEnv env;
    if (!env || gActivity == nullptr) {
        return false;
    }
    jmethodID method = env->GetStaticMethodID(gActivity, "systemBarInsets", "()[I");
    if (method == nullptr || failed(env.get(), "systemBarInsets")) {
        return false;
    }
    jintArray array = (jintArray)env->CallStaticObjectMethod(gActivity, method);
    if (failed(env.get(), "systemBarInsets") || array == nullptr) {
        return false;
    }
    bool ok = false;
    if (env->GetArrayLength(array) == 4) {
        jint values[4] = {0, 0, 0, 0};
        env->GetIntArrayRegion(array, 0, 4, values);
        if (!failed(env.get(), "systemBarInsets")) {
            *left = (int)values[0];
            *top = (int)values[1];
            *right = (int)values[2];
            *bottom = (int)values[3];
            ok = true;
        }
    }
    env->DeleteLocalRef(array);
    return ok;
}

bool AndroidBridge::hasMediaPermission() {
    ScopedEnv env;
    if (!env || gBridge == nullptr) {
        return false;
    }
    jmethodID method = env->GetStaticMethodID(gBridge, "needsPermission", "()Z");
    if (method == nullptr || failed(env.get(), "needsPermission")) {
        return false;
    }
    const jboolean needs = env->CallStaticBooleanMethod(gBridge, method);
    if (failed(env.get(), "needsPermission")) {
        return false;
    }
    return needs == JNI_FALSE;
}

bool AndroidBridge::requestMediaPermission() {
    if (hasMediaPermission()) {
        return true;
    }
    const std::string permission = callStringMethod("permissionName");
    if (permission.empty()) {
        return false;
    }

    PermissionAnswer answer;
    if (!SDL_RequestAndroidPermission(permission.c_str(), onPermission, &answer)) {
        SDL_Log("Could not ask for %s: %s", permission.c_str(), SDL_GetError());
        return false;
    }

    std::unique_lock<std::mutex> lock(answer.mutex);
    answer.ready.wait(lock, [&answer] { return answer.answered; });
    if (!answer.granted) {
        SDL_Log("The photo permission was refused, so the wall stays empty");
    }
    return answer.granted;
}

std::string AndroidBridge::queryBuckets() {
    return callStringMethod("queryBuckets");
}

std::string AndroidBridge::queryBucket(const std::string &bucketId) {
    ScopedEnv env;
    if (!env || gBridge == nullptr) {
        return std::string();
    }
    jmethodID method =
        env->GetStaticMethodID(gBridge, "queryBucket", "(Ljava/lang/String;)Ljava/lang/String;");
    if (method == nullptr || failed(env.get(), "queryBucket")) {
        return std::string();
    }
    jstring argument = env->NewStringUTF(bucketId.c_str());
    jstring result = (jstring)env->CallStaticObjectMethod(gBridge, method, argument);
    std::string text;
    if (!failed(env.get(), "queryBucket")) {
        text = toString(env.get(), result);
    }
    if (result != nullptr) {
        env->DeleteLocalRef(result);
    }
    env->DeleteLocalRef(argument);
    return text;
}

bool AndroidBridge::readImage(int64_t id, std::vector<uint8_t> *bytes) {
    if (bytes == nullptr) {
        return false;
    }
    bytes->clear();

    ScopedEnv env;
    if (!env || gBridge == nullptr) {
        return false;
    }
    jmethodID method = env->GetStaticMethodID(gBridge, "readImage", "(J)[B");
    if (method == nullptr || failed(env.get(), "readImage")) {
        return false;
    }
    jbyteArray array = (jbyteArray)env->CallStaticObjectMethod(gBridge, method, (jlong)id);
    bool ok = false;
    if (!failed(env.get(), "readImage") && array != nullptr) {
        const jsize length = env->GetArrayLength(array);
        if (length > 0) {
            bytes->resize((size_t)length);
            env->GetByteArrayRegion(array, 0, length, (jbyte *)bytes->data());
            ok = !failed(env.get(), "readImage");
        }
    }
    if (array != nullptr) {
        env->DeleteLocalRef(array);
    }
    if (!ok) {
        bytes->clear();
    }
    return ok;
}

namespace {

// Calls a static StorageBridge method that takes a String, or nothing when
// argument is null, and returns a String.
std::string callStorage(const char *name, const std::string *argument) {
    ScopedEnv env;
    if (!env || gStorage == nullptr) {
        return std::string();
    }
    jmethodID method = env->GetStaticMethodID(gStorage, name,
                                              argument != nullptr ? "(Ljava/lang/String;)Ljava/lang/String;"
                                                                  : "()Ljava/lang/String;");
    if (method == nullptr || failed(env.get(), name)) {
        return std::string();
    }
    jstring text = argument != nullptr ? env->NewStringUTF(argument->c_str()) : nullptr;
    jstring result = argument != nullptr ? (jstring)env->CallStaticObjectMethod(gStorage, method, text)
                                         : (jstring)env->CallStaticObjectMethod(gStorage, method);
    std::string answer;
    if (!failed(env.get(), name)) {
        answer = toString(env.get(), result);
    }
    if (result != nullptr) {
        env->DeleteLocalRef(result);
    }
    if (text != nullptr) {
        env->DeleteLocalRef(text);
    }
    return answer;
}

}  // namespace

std::string AndroidBridge::storageSources() {
    return callStorage("sources", nullptr);
}

bool AndroidBridge::pickFolder(const std::string &volume) {
    ScopedEnv env;
    if (!env || gStorage == nullptr) {
        return false;
    }
    jmethodID method = env->GetStaticMethodID(gStorage, "pickFolder", "(Ljava/lang/String;)Z");
    if (method == nullptr || failed(env.get(), "pickFolder")) {
        return false;
    }
    jstring text = env->NewStringUTF(volume.c_str());
    const jboolean opened = env->CallStaticBooleanMethod(gStorage, method, text);
    env->DeleteLocalRef(text);
    return !failed(env.get(), "pickFolder") && opened == JNI_TRUE;
}

Uint32 AndroidBridge::sourceChosenEvent() {
    return gFolderPickedEvent;
}

std::string AndroidBridge::chosenSource() {
    const std::string chosen = callStorage("chosenSource", nullptr);
    return chosen.empty() ? "library" : chosen;
}

void AndroidBridge::setChosenSource(const std::string &id) {
    ScopedEnv env;
    if (!env || gStorage == nullptr) {
        return;
    }
    jmethodID method = env->GetStaticMethodID(gStorage, "setChosenSource", "(Ljava/lang/String;)V");
    if (method == nullptr || failed(env.get(), "setChosenSource")) {
        return;
    }
    jstring text = env->NewStringUTF(id.c_str());
    env->CallStaticVoidMethod(gStorage, method, text);
    env->DeleteLocalRef(text);
    failed(env.get(), "setChosenSource");
}

std::string AndroidBridge::treeName(const std::string &tree) {
    return callStorage("treeName", &tree);
}

Bitmap AndroidBridge::appIcon(const std::string &packageName, int size) {
    ScopedEnv env;
    if (!env || gStorage == nullptr || packageName.empty() || size <= 0) {
        return Bitmap();
    }
    jmethodID method =
        env->GetStaticMethodID(gStorage, "appIcon", "(Ljava/lang/String;I)Landroid/graphics/Bitmap;");
    if (method == nullptr || failed(env.get(), "appIcon")) {
        return Bitmap();
    }
    jstring text = env->NewStringUTF(packageName.c_str());
    jobject image = env->CallStaticObjectMethod(gStorage, method, text, (jint)size);
    env->DeleteLocalRef(text);
    if (failed(env.get(), "appIcon") || image == nullptr) {
        return Bitmap();
    }

    Bitmap icon;
    AndroidBitmapInfo info {};
    void *pixels = nullptr;
    if (AndroidBitmap_getInfo(env.get(), image, &info) == ANDROID_BITMAP_RESULT_SUCCESS &&
        info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 &&
        AndroidBitmap_lockPixels(env.get(), image, &pixels) == ANDROID_BITMAP_RESULT_SUCCESS) {
        icon = Bitmap((int)info.width, (int)info.height);
        if (icon.valid()) {
            // RGBA_8888 is premultiplied and in this port's byte order, so only
            // the stride differs.
            const uint8_t *source = (const uint8_t *)pixels;
            uint8_t *destination = icon.pixels();
            const size_t rowBytes = (size_t)info.width * 4;
            for (unsigned line = 0; line < info.height; ++line) {
                SDL_memcpy(destination, source, rowBytes);
                source += info.stride;
                destination += rowBytes;
            }
            icon.markOpaqueUnlessTransparent();
        }
        AndroidBitmap_unlockPixels(env.get(), image);
    }

    jclass bitmapClass = env->GetObjectClass(image);
    jmethodID recycle = bitmapClass != nullptr ? env->GetMethodID(bitmapClass, "recycle", "()V") : nullptr;
    if (recycle != nullptr) {
        env->CallVoidMethod(image, recycle);
    }
    failed(env.get(), "recycle");
    if (bitmapClass != nullptr) {
        env->DeleteLocalRef(bitmapClass);
    }
    env->DeleteLocalRef(image);
    return icon;
}

std::string AndroidBridge::listFolder(const std::string &folder) {
    return callStorage("listFolder", &folder);
}

std::string AndroidBridge::readExif(const std::string &uri, const std::string &mime) {
    ScopedEnv env;
    if (!env || gStorage == nullptr) {
        return std::string();
    }
    jmethodID method =
        env->GetStaticMethodID(gStorage, "readExif", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (method == nullptr || failed(env.get(), "readExif")) {
        return std::string();
    }
    jstring uriText = env->NewStringUTF(uri.c_str());
    jstring mimeText = env->NewStringUTF(mime.c_str());
    jstring result = (jstring)env->CallStaticObjectMethod(gStorage, method, uriText, mimeText);
    std::string answer;
    if (!failed(env.get(), "readExif")) {
        answer = toString(env.get(), result);
    }
    if (result != nullptr) {
        env->DeleteLocalRef(result);
    }
    env->DeleteLocalRef(mimeText);
    env->DeleteLocalRef(uriText);
    return answer;
}

bool AndroidBridge::readDocument(const std::string &uri, std::vector<uint8_t> *bytes) {
    if (bytes == nullptr) {
        return false;
    }
    bytes->clear();
    ScopedEnv env;
    if (!env || gStorage == nullptr) {
        return false;
    }
    jmethodID method = env->GetStaticMethodID(gStorage, "readDocument", "(Ljava/lang/String;)[B");
    if (method == nullptr || failed(env.get(), "readDocument")) {
        return false;
    }
    jstring text = env->NewStringUTF(uri.c_str());
    jbyteArray array = (jbyteArray)env->CallStaticObjectMethod(gStorage, method, text);
    env->DeleteLocalRef(text);
    bool ok = false;
    if (!failed(env.get(), "readDocument") && array != nullptr) {
        const jsize length = env->GetArrayLength(array);
        if (length > 0) {
            bytes->resize((size_t)length);
            env->GetByteArrayRegion(array, 0, length, (jbyte *)bytes->data());
            ok = !failed(env.get(), "readDocument");
        }
    }
    if (array != nullptr) {
        env->DeleteLocalRef(array);
    }
    if (!ok) {
        bytes->clear();
    }
    return ok;
}

// The folder picker's answer, on Android's main thread. The wall runs on SDL's,
// so the uri travels there as an event.
extern "C" JNIEXPORT void JNICALL Java_me_mariotaku_gallery3d_StorageBridge_nativeFolderPicked(JNIEnv *env, jclass,
                                                                                               jstring tree) {
    if (gFolderPickedEvent == 0) {
        return;
    }
    SDL_Event event;
    SDL_zero(event);
    event.type = gFolderPickedEvent;
    event.user.data1 = tree != nullptr ? new std::string(toString(env, tree)) : nullptr;
    if (!SDL_PushEvent(&event)) {
        delete (std::string *)event.user.data1;
    }
}
