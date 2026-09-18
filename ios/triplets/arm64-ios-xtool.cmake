# The stock arm64-ios triplet assumes a macOS Xcode install. The Linux/WSL
# cross-build delegates to xtool instead, whose toolchain supplies the SDK,
# compilers and linker.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{GALLERY3D_IOS_TOOLCHAIN}")
