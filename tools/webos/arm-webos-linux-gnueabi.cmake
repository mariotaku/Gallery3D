# vcpkg triplet for the openlgtv buildroot SDK, which builds for an armv7
# webOS TV. vcpkg has no webOS triplet of its own, and its stock arm-linux one
# compiles with the host's cross-gcc rather than this SDK's.
#
# Only GLAD and LittleCMS come from vcpkg here. Everything else is either in
# the SDK's sysroot or built from source by the top level CMakeLists.
set(VCPKG_TARGET_ARCHITECTURE arm)
set(VCPKG_CRT_LINKAGE dynamic)
# Static, so the ipk carries no vcpkg libraries of its own. The only shared
# objects it ships are SDL's.
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(NOT DEFINED ENV{WEBOS_SDK})
    message(FATAL_ERROR "Set WEBOS_SDK to the buildroot SDK directory.")
endif()
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "$ENV{WEBOS_SDK}/share/buildroot/toolchainfile.cmake")
