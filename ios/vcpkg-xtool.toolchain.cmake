# vcpkg supplies GLAD for the iOS cross-build, then xtool's toolchain supplies
# the SDK, compilers and linker configuration.
if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "Set VCPKG_ROOT before building the iOS app.")
endif()

set(ENV{GALLERY3D_IOS_TOOLCHAIN} "${CMAKE_CURRENT_LIST_DIR}/xtool.toolchain.cmake")
set(VCPKG_TARGET_TRIPLET arm64-ios-xtool CACHE STRING "" FORCE)
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/triplets" CACHE PATH "" FORCE)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/xtool.toolchain.cmake"
    CACHE FILEPATH "" FORCE)
include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
