#include "platform/posix/StackTrace.h"

#include <emscripten/emscripten.h>

namespace Backtrace {

void printStack() {
    // Use the browser console for source-mapped wasm stacks; execinfo is unavailable.
    emscripten_run_script("console.trace('gallery3d');");
}

}  // namespace Backtrace
