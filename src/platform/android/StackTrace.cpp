// Bionic ships no execinfo, so the frames come from the unwinder and the names
// from the dynamic symbol table.
#include "platform/posix/StackTrace.h"

#include <cstdint>
#include <cstdlib>

#include <cxxabi.h>
#include <dlfcn.h>
#include <unwind.h>

namespace {

// Collects return addresses as the unwinder walks out through the frames.
struct FrameWalk {
    void **frames;
    int count;
    int limit;
};

_Unwind_Reason_Code collectFrame(_Unwind_Context *context, void *argument) {
    FrameWalk *walk = (FrameWalk *)argument;
    const uintptr_t address = _Unwind_GetIP(context);
    if (address != 0 && walk->count < walk->limit) {
        walk->frames[walk->count++] = (void *)address;
    }
    return (walk->count < walk->limit) ? _URC_NO_REASON : _URC_END_OF_STACK;
}

}  // namespace

namespace Backtrace {

void printStack() {
    void *frames[64];
    FrameWalk walk {frames, 0, 64};
    _Unwind_Backtrace(&collectFrame, &walk);

    for (int i = 1; i < walk.count; ++i) {
        Dl_info info {};
        if (dladdr(frames[i], &info) == 0 || info.dli_sname == nullptr) {
            // Outside any exported symbol, which is most of a stripped release
            // build. The offset into the library is what addr2line needs.
            const char *library = (info.dli_fname != nullptr) ? info.dli_fname : "??";
            const uintptr_t offset = (uintptr_t)frames[i] - (uintptr_t)info.dli_fbase;
            say("  #%-2d %s+0x%zx", i - 1, library, (size_t)offset);
            continue;
        }
        int status = 0;
        char *readable = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        const uintptr_t offset = (uintptr_t)frames[i] - (uintptr_t)info.dli_saddr;
        say("  #%-2d %s+0x%zx", i - 1, (status == 0 && readable != nullptr) ? readable : info.dli_sname,
            (size_t)offset);
        free(readable);
    }
}

}  // namespace Backtrace
