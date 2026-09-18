# vcpkg must be the primary toolchain so it can put its generated GLAD package
# on CMake's package path. It then delegates platform setup to the NDK toolchain
# Gradle already selects for each ABI.
if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "Set VCPKG_ROOT before building the Android app.")
endif()
if(DEFINED ANDROID_NDK)
    set(_gallery3d_ndk "${ANDROID_NDK}")
elseif(DEFINED CMAKE_ANDROID_NDK)
    set(_gallery3d_ndk "${CMAKE_ANDROID_NDK}")
elseif(DEFINED ENV{ANDROID_NDK_HOME})
    set(_gallery3d_ndk "$ENV{ANDROID_NDK_HOME}")
else()
    message(FATAL_ERROR "The Android NDK was not supplied by Gradle or ANDROID_NDK_HOME.")
endif()

if(DEFINED ANDROID_ABI)
    set(_gallery3d_android_abi "${ANDROID_ABI}")
else()
    set(_gallery3d_android_abi "${CMAKE_ANDROID_ARCH_ABI}")
endif()
if(_gallery3d_android_abi STREQUAL "arm64-v8a")
    set(_gallery3d_vcpkg_triplet arm64-android)
elseif(_gallery3d_android_abi STREQUAL "x86_64")
    set(_gallery3d_vcpkg_triplet x64-android)
else()
    message(FATAL_ERROR "No vcpkg triplet is configured for Android ABI '${_gallery3d_android_abi}'.")
endif()

set(VCPKG_TARGET_TRIPLET "${_gallery3d_vcpkg_triplet}" CACHE STRING "" FORCE)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_gallery3d_ndk}/build/cmake/android.toolchain.cmake"
    CACHE FILEPATH "" FORCE)
# The Android vcpkg triplets load vcpkg's Android toolchain during package
# builds. That toolchain takes the NDK path from this environment variable.
set(ENV{ANDROID_NDK_HOME} "${_gallery3d_ndk}")
include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
