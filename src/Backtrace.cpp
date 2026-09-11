#include "Backtrace.h"

#include <atomic>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <windows.h>
// After windows.h, which dbghelp.h needs.
#include <dbghelp.h>
#include <crtdbg.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <unistd.h>
#include <cstring>
#else
#include <cxxabi.h>
#include <execinfo.h>
#include <unistd.h>
#include <cstring>
#endif

namespace {

// Only one thread gets to print. A crash often takes several down at once, and
// interleaved stacks are worse than one stack.
std::atomic<bool> gPrinting{false};

// Straight to stderr rather than SDL_Log. By the time this runs the process is
// already wrong, and the fewer locks taken on the way out the better.
void say(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

#if defined(_WIN32)

// How many frames to walk. Deep enough for a decode running under the feed's
// worker thread, which is about the longest stack here.
const int kMaxFrames = 62;

void printStack(CONTEXT *context) {
    HANDLE process = GetCurrentProcess();
    void *frames[kMaxFrames];
    USHORT count = 0;

    if (context != nullptr) {
        // A crash: walk the context the exception arrived with, not this
        // handler's own stack.
        STACKFRAME64 frame = {};
        CONTEXT walked = *context;
        DWORD machine;
#if defined(_M_X64)
        machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = walked.Rip;
        frame.AddrFrame.Offset = walked.Rbp;
        frame.AddrStack.Offset = walked.Rsp;
#elif defined(_M_ARM64)
        machine = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = walked.Pc;
        frame.AddrFrame.Offset = walked.Fp;
        frame.AddrStack.Offset = walked.Sp;
#else
        machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = walked.Eip;
        frame.AddrFrame.Offset = walked.Ebp;
        frame.AddrStack.Offset = walked.Esp;
#endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
        while (count < kMaxFrames &&
               StackWalk64(machine, process, GetCurrentThread(), &frame, &walked, nullptr,
                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            if (frame.AddrPC.Offset == 0) {
                break;
            }
            frames[count++] = (void *)(uintptr_t)frame.AddrPC.Offset;
        }
    } else {
        count = CaptureStackBackTrace(1, kMaxFrames, frames, nullptr);
    }

    // SYMBOL_INFO carries the name past the end of the struct, so it needs room
    // behind it.
    char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    for (USHORT i = 0; i < count; ++i) {
        DWORD64 address = (DWORD64)(uintptr_t)frames[i];
        DWORD64 displacement = 0;
        const char *name = "??";
        if (SymFromAddr(process, address, &displacement, symbol)) {
            name = symbol->Name;
        }

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD column = 0;
        if (SymGetLineFromAddr64(process, address, &column, &line)) {
            say("  #%-2d %s  %s:%lu", (int)i, name, line.FileName, line.LineNumber);
        } else {
            say("  #%-2d %s  +0x%llx", (int)i, name, (unsigned long long)displacement);
        }
    }
    if (count == 0) {
        say("  (no frames - the stack is too damaged to walk)");
    }
}

const char *describe(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "access violation";
        case EXCEPTION_STACK_OVERFLOW:
            return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "illegal instruction";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "misaligned access";
        case 0xC0000409:
            return "fail fast (a CRT or /GS check tripped)";
        case 0xE06D7363:
            return "unhandled C++ exception";
        default:
            return "fatal exception";
    }
}

void report(const char *reason, EXCEPTION_POINTERS *pointers) {
    if (gPrinting.exchange(true)) {
        // Another thread is already printing. Let it finish rather than
        // interleaving, and do not return into broken code.
        Sleep(5000);
        return;
    }
    say("");
    say("--- %s ---", reason);
    if (pointers != nullptr && pointers->ExceptionRecord != nullptr) {
        EXCEPTION_RECORD *record = pointers->ExceptionRecord;
        say("code 0x%08lX (%s) at %p", record->ExceptionCode, describe(record->ExceptionCode),
            record->ExceptionAddress);
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            const char *what = record->ExceptionInformation[0] != 0 ? "writing" : "reading";
            say("%s address %p", what, (void *)record->ExceptionInformation[1]);
        }
    }
    say("thread %lu:", GetCurrentThreadId());
    printStack(pointers != nullptr ? pointers->ContextRecord : nullptr);
    say("--- end of trace ---");
    say("");
    gPrinting = false;
}

// A trace names the frames. A minidump carries every thread, its registers and
// enough memory to read the arguments, and a debugger opens it like a live
// process. Worth the few lines when the alternative is reproducing the crash.
void writeMinidump(EXCEPTION_POINTERS *pointers) {
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH - 16) {
        return;
    }
    // Next to the binary, stamped so one run does not overwrite the last.
    SYSTEMTIME now;
    GetLocalTime(&now);
    char *dot = strrchr(path, '.');
    if (dot != nullptr) {
        *dot = 0;
    }
    char name[MAX_PATH + 32];
    snprintf(name, sizeof(name), "%s-%02d%02d%02d-%lu.dmp", path, now.wHour, now.wMinute, now.wSecond,
             GetCurrentProcessId());

    HANDLE file = CreateFileA(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info = {};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = pointers;
    info.ClientPointers = FALSE;
    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                                         MiniDumpWithThreadInfo);
    BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                     pointers != nullptr ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
    if (written) {
        say("minidump written to %s", name);
    }
}

LONG WINAPI onUnhandled(EXCEPTION_POINTERS *pointers) {
    report("crash", pointers);
    writeMinidump(pointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

// A vectored handler runs before the filter above, so it sees a few things the
// filter never will. It cannot see __fastfail, which leaves through the kernel
// without raising anything - the invalid parameter handler below is what covers
// that, by running before the CRT gets that far. This one waves everything
// except its own short list straight through.
LONG WINAPI onVectored(EXCEPTION_POINTERS *pointers) {
    if (pointers == nullptr || pointers->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD code = pointers->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000409 && code != EXCEPTION_STACK_OVERFLOW) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    report("crash", pointers);
    return EXCEPTION_CONTINUE_SEARCH;
}

void onInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t) {
    // The CRT calls this before it fails fast, and that is the only chance to
    // see what asked it to. With no handler installed it goes out as a bare
    // 0xC0000409 and says nothing.
    report("a CRT call got an argument it refuses", nullptr);
    _exit(3);
}

void onAbort(int) {
    // abort() raises SIGABRT before it goes, and assert and the terminate
    // handler both end here.
    report("abort", nullptr);
    _exit(3);
}

#elif defined(__EMSCRIPTEN__)

void printStack() {
    // The browser knows the stack, not us: there is no execinfo here, and the
    // wasm frames are the engine's to name. emscripten_run_script hands the
    // job to the JS console, which prints it with the source map applied.
    emscripten_run_script("console.trace('gallery3d');");
}

void report(const char *reason) {
    if (gPrinting.exchange(true)) {
        return;
    }
    say("");
    say("--- %s ---", reason);
    printStack();
    say("--- end of trace ---");
    gPrinting = false;
}

void onSignal(int number) {
    report(strsignal(number));
    signal(number, SIG_DFL);
    raise(number);
}

#else

void printStack() {
    void *frames[64];
    int count = backtrace(frames, 64);
    char **names = backtrace_symbols(frames, count);
    for (int i = 1; i < count; ++i) {
        say("  #%-2d %s", i - 1, names != nullptr ? names[i] : "??");
    }
    free(names);
}

void report(const char *reason) {
    if (gPrinting.exchange(true)) {
        return;
    }
    say("");
    say("--- %s ---", reason);
    printStack();
    say("--- end of trace ---");
    say("");
    gPrinting = false;
}

void onSignal(int number) {
    report(strsignal(number));
    signal(number, SIG_DFL);
    raise(number);
}

#endif

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
#if defined(_WIN32)
    report(what, nullptr);
#else
    report(what);
#endif
    _exit(3);
}

}  // namespace

namespace Backtrace {

void install() {
#if defined(_WIN32)
    // No dialogs on the way out. Windows offers to report the fault and waits
    // for a click, and that click never comes from a scripted run - the trace
    // below it never gets printed.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    SetUnhandledExceptionFilter(onUnhandled);
    AddVectoredExceptionHandler(1, onVectored);
    _set_invalid_parameter_handler(onInvalidParameter);
    signal(SIGABRT, onAbort);
    // No "this application has requested the runtime to terminate" box. It
    // waits for a click that a scripted run will never make.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // Otherwise an assert opens a dialog and waits, which is no use when the
    // app was started by a script.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#else
    signal(SIGSEGV, onSignal);
    signal(SIGBUS, onSignal);
    signal(SIGFPE, onSignal);
    signal(SIGILL, onSignal);
    signal(SIGABRT, onSignal);
#endif
    std::set_terminate(onTerminate);

    // SDL_assert puts up its own dialog and waits there too. Told to abort, it
    // leaves through SIGABRT and comes out as a trace like anything else. This
    // is a hint rather than a call, so it works before SDL_Init.
    SDL_SetHint(SDL_HINT_ASSERT, "abort");
}

void print(const char *reason) {
#if defined(_WIN32)
    report(reason, nullptr);
#else
    report(reason);
#endif
}

}  // namespace Backtrace
