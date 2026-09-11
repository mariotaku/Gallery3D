// Prints a stack trace when the process dies.
//
// Worth having because the alternative is guessing. A crash inside a loader
// thread shows up as an exit code and nothing else, and the code is the same
// whichever mistake caused it.
//
// Symbols come from the pdb next to the binary on Windows, so a release build
// names its frames too as long as CMake was told to keep them.
#pragma once

namespace Backtrace {

// Installs the handler. Call once, early.
void install();

// Prints the current stack to the log, most recent frame first. Useful on its
// own when something is merely wrong rather than fatal.
void print(const char *reason);

}  // namespace Backtrace
