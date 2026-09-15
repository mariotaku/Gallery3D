#include "core/Backtrace.h"

#include <atomic>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <SDL3/SDL.h>

#include <windows.h>
// After windows.h, which dbghelp.h needs.
#include <dbghelp.h>
#include <crtdbg.h>

namespace {

// Serialize crash reports to prevent interleaved stacks.
std::atomic<bool> gPrinting{false};

// Write directly to stderr to avoid SDL_Log locks during a crash.
void say(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

// Maximum stack depth.
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

// Write a minidump containing thread contexts and memory for debugger inspection.
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

// Vectored handlers precede the exception filter but cannot catch __fastfail.
// The CRT invalid-parameter handler covers that path before termination.
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
    // The CRT calls this before fail-fast termination.
    report("a CRT call got an argument it refuses", nullptr);
    _exit(3);
}

void onAbort(int) {
    // abort() raises SIGABRT before it goes, and assert and the terminate
    // handler both end here.
    report("abort", nullptr);
    _exit(3);
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
    report(what, nullptr);
    _exit(3);
}

}  // namespace

namespace Backtrace {

void install() {
    // Disable Windows fault dialogs so unattended crashes reach the trace handler.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // Once only. install runs again after SDL_Init to put the filter back on
    // top of SDL's, and a second vectored handler would report every overflow
    // twice, as a second SymInitialize would fail.
    static bool installedOnce = false;
    if (!installedOnce) {
        installedOnce = true;
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        AddVectoredExceptionHandler(1, onVectored);
    }
    SetUnhandledExceptionFilter(onUnhandled);
    _set_invalid_parameter_handler(onInvalidParameter);
    signal(SIGABRT, onAbort);
    // Disable the CRT runtime-error dialog for unattended runs.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // Disable assert dialogs for unattended runs.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    std::set_terminate(onTerminate);

    // Route SDL assertions through SIGABRT. The hint works before SDL_Init.
    SDL_SetHint(SDL_HINT_ASSERT, "abort");
}

void print(const char *reason) {
    report(reason, nullptr);
}

}  // namespace Backtrace
