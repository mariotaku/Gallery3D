# vcpkg must be the primary toolchain so it can put its generated GLAD and
# LittleCMS packages on CMake's package path. It then delegates platform setup
# to the buildroot SDK's own toolchain file.
if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "Set VCPKG_ROOT before building for webOS.")
endif()
if(NOT DEFINED ENV{WEBOS_SDK})
    message(FATAL_ERROR "Set WEBOS_SDK to the buildroot SDK directory.")
endif()

set(VCPKG_TARGET_TRIPLET "arm-webos-linux-gnueabi" CACHE STRING "" FORCE)
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}" CACHE STRING "" FORCE)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "$ENV{WEBOS_SDK}/share/buildroot/toolchainfile.cmake" CACHE FILEPATH "" FORCE)
# LittleCMS is a webOS-only dependency: the distros package it and the
# buildroot sysroot does not carry it. A manifest feature keeps it off the
# other platforms' builds.
set(VCPKG_MANIFEST_FEATURES "webos" CACHE STRING "" FORCE)
include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

# The buildroot toolchain, which vcpkg chainloads above, sets
# CMAKE_FIND_ROOT_PATH_MODE_PACKAGE to ONLY and points the find root at its own
# sysroot. A prefix outside that root is then invisible to find_package, however
# CMAKE_PREFIX_PATH names it. vcpkg adds its own installed tree to the find root
# for this reason; the prebuilt SDL3 needs the same.
if(GALLERY3D_WEBOS_PREFIX)
    list(APPEND CMAKE_FIND_ROOT_PATH "${GALLERY3D_WEBOS_PREFIX}")
endif()

# The buildroot toolchain points pkg-config's sysroot at the SDK but leaves its
# search path alone, so pkg-config reads the build machine's .pc files and
# answers with the host's include directories under the SDK's prefix. Search the
# sysroot instead.
set(_webos_sysroot "$ENV{WEBOS_SDK}/arm-webos-linux-gnueabi/sysroot")
set(ENV{PKG_CONFIG_LIBDIR}
    "${_webos_sysroot}/usr/lib/pkgconfig:${_webos_sysroot}/usr/share/pkgconfig")
unset(_webos_sysroot)
