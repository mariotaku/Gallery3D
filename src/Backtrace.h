// Prints a stack trace on process failure. Windows symbols come from the PDB
// next to the binary, including Release builds configured to keep symbols.
#pragma once

namespace Backtrace {

// Installs the handler. Call once, early.
void install();

// Logs the current stack, most recent frame first.
void print(const char *reason);

}  // namespace Backtrace
