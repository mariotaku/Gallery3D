# The crash handler's signals and local time, done the same way on every
# platform that is not Windows.
target_sources(gallery3d_core PRIVATE
    Backtrace.cpp
    LocalTime.cpp
    StackTrace.h)

# The freedesktop.org trash and opening a file by its URI, which only a desktop
# has. Android and iOS take the generic answer instead.
if(NOT ANDROID AND NOT IOS)
    target_sources(gallery3d_core PRIVATE
        FileOperations.cpp)
endif()

# Walking the stack through execinfo. Android's C library has no execinfo, so
# it brings its own.
if(NOT ANDROID)
    target_sources(gallery3d_core PRIVATE
        StackTrace.cpp)
endif()
