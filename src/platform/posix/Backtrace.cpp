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

#include "platform/posix/StackTrace.h"

namespace {

// Serialize crash reports to prevent interleaved stacks.
std::atomic<bool> gPrinting{false};

void report(const char *reason) {
    if (gPrinting.exchange(true)) {
        return;
    }
    Backtrace::say("");
    Backtrace::say("--- %s ---", reason);
    Backtrace::printStack();
    Backtrace::say("--- end of trace ---");
    Backtrace::say("");
    gPrinting = false;
}

void onSignal(int number) {
    report(strsignal(number));
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
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
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
