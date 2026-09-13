#include "platform/posix/StackTrace.h"

#include <cstdlib>

#include <execinfo.h>

namespace Backtrace {

void printStack() {
    void *frames[64];
    int count = backtrace(frames, 64);
    char **names = backtrace_symbols(frames, count);
    for (int i = 1; i < count; ++i) {
        say("  #%-2d %s", i - 1, names != nullptr ? names[i] : "??");
    }
    free(names);
}

}  // namespace Backtrace
