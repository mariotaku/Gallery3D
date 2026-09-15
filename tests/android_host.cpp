// The Android host for the tests: the app's main library in the conformance
// build type. It has two ways in.
//
// SDLActivity calls main() here instead of the wall's. It copies the fixtures
// out of the apk into internal storage, because several tests open fixtures by
// path, runs the tests named by the intent's "filter" extra, and writes every
// line of the report to logcat and to files/test-results.txt.
// files/test-done, holding the number of failed checks, tells
// scripts/android-tests.sh the run is over.
//
// The instrumentation test ConformanceTest calls the JNI functions at the end
// instead, and runs each test as a JUnit test of its own.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/log.h>
#include <jni.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/Backtrace.h"
#include "graphics/DrawableLoad.h"
#include "graphics/RegionDecoder.h"
#include "graphics/SubsampledDecode.h"
#include "platform/android/AndroidBridge.h"
#include "test_runner.h"

namespace {

const char *const kLogTag = "Gallery3DTests";

// As the wall does, on a thread that can look an app class up by name: the one
// SDLActivity runs main() on, or the one the instrumentation calls in from.
// The tests reach the platform through these.
void initBridges() {
    AndroidBridge::init();
    RegionDecoder::initAndroid();
    SubsampledDecode::init();
    DrawableLoad::init();
}

std::string toString(JNIEnv *env, jstring value) {
    std::string result;
    if (value == nullptr) {
        return result;
    }
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars != nullptr) {
        result = chars;
        env->ReleaseStringUTFChars(value, chars);
    }
    return result;
}

// The folders a path's file sits in, made where they are missing.
void makeParents(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        SDL_CreateDirectory(path.substr(0, slash).c_str());
    }
}

// Copies every file fixtures/index.txt names out of the apk's assets into
// folder. False when the index cannot be read or a file cannot be written.
bool copyFixtures(const std::string &folder) {
    size_t size = 0;
    char *index = (char *)SDL_LoadFile("fixtures/index.txt", &size);
    if (index == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "No fixtures/index.txt in the apk");
        return false;
    }
    const std::string names(index, size);
    SDL_free(index);

    bool copied = true;
    size_t start = 0;
    while (start < names.size()) {
        size_t end = names.find('\n', start);
        if (end == std::string::npos) {
            end = names.size();
        }
        const std::string name = names.substr(start, end - start);
        start = end + 1;
        if (name.empty()) {
            continue;
        }
        size_t length = 0;
        void *bytes = SDL_LoadFile(("fixtures/" + name).c_str(), &length);
        const std::string target = folder + "/" + name;
        makeParents(target);
        if (bytes == nullptr || !SDL_SaveFile(target.c_str(), bytes, length)) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Could not copy fixture %s", name.c_str());
            copied = false;
        }
        SDL_free(bytes);
    }
    return copied;
}

// The launch intent's "filter" extra, or empty.
std::string filterExtra() {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    std::string filter;
    if (env == nullptr || activity == nullptr) {
        return filter;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getIntent = env->GetMethodID(activityClass, "getIntent", "()Landroid/content/Intent;");
    jobject intent = getIntent != nullptr ? env->CallObjectMethod(activity, getIntent) : nullptr;
    if (intent != nullptr) {
        jclass intentClass = env->GetObjectClass(intent);
        jmethodID getStringExtra =
            env->GetMethodID(intentClass, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
        jstring key = env->NewStringUTF("filter");
        jstring value = getStringExtra != nullptr ? (jstring)env->CallObjectMethod(intent, getStringExtra, key) : nullptr;
        if (value != nullptr) {
            const char *chars = env->GetStringUTFChars(value, nullptr);
            if (chars != nullptr) {
                filter = chars;
                env->ReleaseStringUTFChars(value, chars);
            }
            env->DeleteLocalRef(value);
        }
        env->DeleteLocalRef(key);
        env->DeleteLocalRef(intentClass);
        env->DeleteLocalRef(intent);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    // am start passes the quotes the shell kept around an empty filter.
    if (filter == "''") {
        filter.clear();
    }
    return filter;
}

}  // namespace

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    Backtrace::install();
    initBridges();

    const char *internal = SDL_GetAndroidInternalStoragePath();
    const std::string files = internal != nullptr ? internal : ".";
    const std::string resultsPath = files + "/test-results.txt";
    const std::string donePath = files + "/test-done";
    std::remove(resultsPath.c_str());
    std::remove(donePath.c_str());
    std::FILE *results = std::fopen(resultsPath.c_str(), "w");

    const std::string fixtures = files + "/fixtures";
    const bool copied = copyFixtures(fixtures);

    TestRunner::Options options;
    options.filter = filterExtra();
    // The apk's assets folder, which App::assetPath joins names onto.
    options.assetRoot = "";
    // Where the copy went: tests/fixtures' own layout under it.
    options.fixtureRoot = fixtures;
    options.print = [results](const std::string &line) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", line.c_str());
        if (results != nullptr) {
            std::fprintf(results, "%s\n", line.c_str());
            std::fflush(results);
        }
    };
    if (!copied) {
        options.print("Some fixtures could not be copied out of the apk");
    }
    const int failures = TestRunner::run(options);

    if (results != nullptr) {
        std::fclose(results);
    }
    if (std::FILE *done = std::fopen(donePath.c_str(), "w")) {
        std::fprintf(done, "%d\n", failures);
        std::fclose(done);
    }
    return failures == 0 ? 0 : 1;
}

extern "C" {

JNIEXPORT jobjectArray JNICALL Java_me_mariotaku_gallery3d_ConformanceTest_nativeNames(JNIEnv *env, jclass) {
    const std::vector<std::string> names = TestRunner::names();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray array = env->NewObjectArray((jsize)names.size(), stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    if (array == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < names.size(); ++i) {
        // Test names are C identifiers, so plain ASCII.
        jstring name = env->NewStringUTF(names[i].c_str());
        env->SetObjectArrayElement(array, (jsize)i, name);
        env->DeleteLocalRef(name);
    }
    return array;
}

JNIEXPORT jboolean JNICALL Java_me_mariotaku_gallery3d_ConformanceTest_nativeSetUp(JNIEnv *env, jclass,
                                                                                    jstring fixtureRoot) {
    Backtrace::install();
    initBridges();
    return copyFixtures(toString(env, fixtureRoot)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL Java_me_mariotaku_gallery3d_ConformanceTest_nativeRun(JNIEnv *env, jclass, jstring name,
                                                                                   jstring fixtureRoot,
                                                                                   jintArray counts) {
    std::string report;
    TestRunner::Options options;
    options.name = toString(env, name);
    options.assetRoot = "";
    options.fixtureRoot = toString(env, fixtureRoot);
    options.print = [&report](const std::string &line) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", line.c_str());
        report += line;
        report += '\n';
    };
    TestRunner::Summary summary;
    const int failures = TestRunner::run(options, &summary);

    if (counts != nullptr && env->GetArrayLength(counts) >= 3) {
        const jint values[3] = {failures, summary.skipped, summary.tests};
        env->SetIntArrayRegion(counts, 0, 3, values);
    }
    // Bytes rather than a String: a report can quote text with characters
    // outside the modified UTF-8 that NewStringUTF accepts.
    jbyteArray bytes = env->NewByteArray((jsize)report.size());
    if (bytes != nullptr) {
        env->SetByteArrayRegion(bytes, 0, (jsize)report.size(), (const jbyte *)report.data());
    }
    return bytes;
}

}  // extern "C"
