// The part of the crash handler that differs between the platforms that are
// not Windows: walking the stack. posix/Backtrace.cpp does everything else,
// the same way on all of them.
#pragma once

namespace Backtrace {

// Writes one line straight to stderr. SDL_Log takes locks, which the crash
// may have left held.
void say(const char *format, ...);

// Prints the current stack through say(), most recent frame first.
void printStack();

}  // namespace Backtrace
