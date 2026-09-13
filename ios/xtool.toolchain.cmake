# Cross-compiles for an iPhone from Linux, with the SDK xtool extracts out of
# Xcode.xip and the compiler that comes with the Swift toolchain.
#
# `xtool setup` leaves the SDK in a Swift SDK bundle. Only its iPhoneOS.sdk is
# read here: the headers, the frameworks and the stubs to link against. The
# compiler is the Swift toolchain's clang, which targets Darwin, and it finds
# ld64.lld beside itself when asked for lld.
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Oldest iOS the app runs on")

set(XTOOL_SDK_BUNDLE "$ENV{HOME}/.swiftpm/swift-sdks/darwin.artifactbundle"
    CACHE PATH "The Swift SDK bundle xtool setup installs")

file(GLOB _gallery3d_ios_sdks
    "${XTOOL_SDK_BUNDLE}/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS*.sdk")
if(NOT _gallery3d_ios_sdks)
    message(FATAL_ERROR
        "No iPhoneOS SDK under ${XTOOL_SDK_BUNDLE}. Run `xtool setup` with an Xcode.xip first.")
endif()
# The newest, if Xcode ever ships more than one.
list(SORT _gallery3d_ios_sdks COMPARE NATURAL)
list(GET _gallery3d_ios_sdks -1 _gallery3d_ios_sdk)
set(CMAKE_OSX_SYSROOT "${_gallery3d_ios_sdk}" CACHE PATH "" FORCE)

# swiftly puts the toolchain's tools on PATH. Its clang, not a distro one: an
# older clang may not know the SDK's headers, and only this one has ld64.lld
# beside it.
set(_gallery3d_swift_bin "$ENV{HOME}/.local/share/swiftly/bin")
find_program(GALLERY3D_IOS_CLANG clang HINTS "${_gallery3d_swift_bin}" NO_DEFAULT_PATH)
find_program(GALLERY3D_IOS_CLANG clang REQUIRED)
find_program(GALLERY3D_IOS_CLANGXX clang++ HINTS "${_gallery3d_swift_bin}" NO_DEFAULT_PATH)
find_program(GALLERY3D_IOS_CLANGXX clang++ REQUIRED)
find_program(GALLERY3D_IOS_AR llvm-ar HINTS "${_gallery3d_swift_bin}" NO_DEFAULT_PATH)
find_program(GALLERY3D_IOS_AR llvm-ar REQUIRED)
find_program(GALLERY3D_IOS_RANLIB llvm-ranlib HINTS "${_gallery3d_swift_bin}" NO_DEFAULT_PATH)
find_program(GALLERY3D_IOS_RANLIB llvm-ranlib REQUIRED)

set(_gallery3d_ios_target "arm64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}")
foreach(lang C CXX OBJC OBJCXX)
    set(CMAKE_${lang}_COMPILER_TARGET "${_gallery3d_ios_target}")
endforeach()
set(CMAKE_C_COMPILER "${GALLERY3D_IOS_CLANG}")
set(CMAKE_OBJC_COMPILER "${GALLERY3D_IOS_CLANG}")
set(CMAKE_CXX_COMPILER "${GALLERY3D_IOS_CLANGXX}")
set(CMAKE_OBJCXX_COMPILER "${GALLERY3D_IOS_CLANGXX}")
set(CMAKE_AR "${GALLERY3D_IOS_AR}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${GALLERY3D_IOS_RANLIB}" CACHE FILEPATH "" FORCE)

# CMake refuses to configure an Apple target without install_name_tool, even
# though it only rewrites the paths inside shared libraries and everything here
# links statically. The Swift toolchain has none; the distro's LLVM does.
file(GLOB _gallery3d_llvm_bins "/usr/lib/llvm-*/bin")
find_program(GALLERY3D_IOS_INSTALL_NAME_TOOL
    NAMES llvm-install-name-tool install_name_tool
    HINTS "${_gallery3d_swift_bin}" ${_gallery3d_llvm_bins})
if(NOT GALLERY3D_IOS_INSTALL_NAME_TOOL)
    message(FATAL_ERROR "No install_name_tool. On Debian or Ubuntu: sudo apt-get install llvm")
endif()
set(CMAKE_INSTALL_NAME_TOOL "${GALLERY3D_IOS_INSTALL_NAME_TOOL}" CACHE FILEPATH "" FORCE)

# Apple's ld is not here to be found, so clang is told lld and runs ld64.lld.
# Not the Swift toolchain's own copy, which is built without iOS ("does not
# support linking for platform iOS"), but the one xtool puts in the SDK
# bundle: -B makes clang look in that folder before its own.
set(_gallery3d_ios_linker_dir "${XTOOL_SDK_BUNDLE}/toolset/bin")
if(NOT EXISTS "${_gallery3d_ios_linker_dir}/ld64.lld")
    message(FATAL_ERROR "No ld64.lld in ${_gallery3d_ios_linker_dir}. Run `xtool sdk install` again.")
endif()
# Xcode's compiler runtime for iOS, which the Swift toolchain for Linux does
# not carry. SDL's Objective-C calls @available, and every such check is a call
# into __isPlatformVersionAtLeast in here.
file(GLOB _gallery3d_ios_builtins
    "${XTOOL_SDK_BUNDLE}/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/*/lib/darwin/libclang_rt.ios.a")
if(NOT _gallery3d_ios_builtins)
    message(FATAL_ERROR "No libclang_rt.ios.a in ${XTOOL_SDK_BUNDLE}. Run `xtool sdk install` again.")
endif()
list(GET _gallery3d_ios_builtins -1 _gallery3d_ios_builtins)
set(_gallery3d_ios_link_flags "-fuse-ld=lld -B${_gallery3d_ios_linker_dir} ${_gallery3d_ios_builtins}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gallery3d_ios_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_gallery3d_ios_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_gallery3d_ios_link_flags}")

# Libraries and headers come from the SDK only, while the build still runs
# the host's programs.
set(CMAKE_FIND_ROOT_PATH "${CMAKE_OSX_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Frameworks live in the SDK, and find_library needs to look there for UIKit
# and the rest.
set(CMAKE_FIND_FRAMEWORK FIRST)
