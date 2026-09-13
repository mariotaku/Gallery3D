# The crash handler's signals, the trash, opening a file and local time, done
# the same way on every platform that is not Windows.
target_sources(gallery3d_core PRIVATE
    Backtrace.cpp
    FileOperations.cpp
    LocalTime.cpp
    StackTrace.h)

# Walking the stack through execinfo. Android's C library has no execinfo and
# the browser has no native stack to walk, so each brings its own.
if(NOT ANDROID AND NOT EMSCRIPTEN)
    target_sources(gallery3d_core PRIVATE
        StackTrace.cpp)
endif()
