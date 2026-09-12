#include "AndroidBridge.h"

#if defined(__ANDROID__)

#include <jni.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <SDL3/SDL.h>

namespace {

const char *const kBridgeClass = "me/mariotaku/gallery3d/MediaStoreBridge";
const char *const kActivityClass = "me/mariotaku/gallery3d/MainActivity";

// A global reference to the helper class, taken once on the thread that runs
// main(). FindClass resolves against the class loader of the calling thread,
// and a thread attached later carries only the bootstrap loader, which knows
// nothing about the app's own classes.
jclass gBridge = nullptr;
jclass gActivity = nullptr;

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

#endif  // __ANDROID__
