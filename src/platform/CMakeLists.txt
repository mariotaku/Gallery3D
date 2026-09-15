# The parts that differ by platform. A shared header has exactly one
# implementation compiled, named after the header and kept in the directory
# of the platforms it is for:
#
#   windows/  Windows
#   android/  Android
#   posix/    every platform that is not Windows
#   desktop/  Linux and macOS: libjpeg and LittleCMS, where there is no WIC
#   generic/  fallbacks for a platform without an implementation of its own
#
# Each directory's own list says which of its files a platform takes.

set(GALLERY3D_DESKTOP ON)
if(ANDROID OR IOS)
    set(GALLERY3D_DESKTOP OFF)
endif()

if(WIN32)
    add_subdirectory(windows)
else()
    add_subdirectory(posix)
endif()
if(ANDROID)
    add_subdirectory(android)
endif()
if(GALLERY3D_DESKTOP)
    add_subdirectory(desktop)
endif()
add_subdirectory(generic)
