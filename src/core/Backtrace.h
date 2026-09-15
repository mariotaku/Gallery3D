// Prints a stack trace on process failure. Windows symbols come from the PDB
// next to the binary, including Release builds configured to keep symbols.
#pragma once

namespace Backtrace {

// Installs the handler. Call it first thing, and again after anything that puts
// its own handler on top, such as SDL_Init. A second call only puts this one
// back on top: nothing is registered twice.
void install();

// Logs the current stack, most recent frame first.
void print(const char *reason);

}  // namespace Backtrace
