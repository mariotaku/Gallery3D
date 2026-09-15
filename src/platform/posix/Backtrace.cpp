#include "core/Backtrace.h"

#include <atomic>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <SDL3/SDL.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "platform/posix/StackTrace.h"

namespace {

// Serialize crash reports to prevent interleaved stacks.
std::atomic<bool> gPrinting{false};

// False when another thread is printing its own report.
bool report(const char *reason) {
    if (gPrinting.exchange(true)) {
        return false;
    }
    Backtrace::say("");
    Backtrace::say("--- %s ---", reason);
    Backtrace::printStack();
    Backtrace::say("--- end of trace ---");
    Backtrace::say("");
    gPrinting = false;
    return true;
}

void onSignal(int number) {
    if (!report(strsignal(number))) {
        // Two threads can fault on the same freed memory together. Ending the
        // process now would cut the other report short.
        sleep(2);
    }
    signal(number, SIG_DFL);
    raise(number);
}

void onTerminate() {
    const char *what = "std::terminate";
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception &error) {
            what = error.what();
        } catch (...) {
        }
    }
    report(what);
    _exit(3);
}

}  // namespace

namespace Backtrace {

void say(const char *format, ...) {
    va_list args;
    va_start(args, format);
#if defined(__ANDROID__)
    // An app's stderr goes nowhere on Android. The crash buffer is what adb
    // logcat -b crash shows, where logd lets the app write to it.
    char line[512];
    vsnprintf(line, sizeof(line), format, args);
    __android_log_write(ANDROID_LOG_FATAL, "Gallery3D", line);
    __android_log_buf_write(LOG_ID_CRASH, ANDROID_LOG_FATAL, "Gallery3D", line);
#else
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    fflush(stderr);
#endif
    va_end(args);
}

void install() {
    signal(SIGSEGV, onSignal);
    signal(SIGBUS, onSignal);
    signal(SIGFPE, onSignal);
    signal(SIGILL, onSignal);
    signal(SIGABRT, onSignal);
    std::set_terminate(onTerminate);

    // Route SDL assertions through SIGABRT. The hint works before SDL_Init.
    SDL_SetHint(SDL_HINT_ASSERT, "abort");
}

void print(const char *reason) {
    report(reason);
}

}  // namespace Backtrace
