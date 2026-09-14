cmake_minimum_required(VERSION 3.21)
project(gallery3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    # The web build has no vcpkg to ask, so the dependencies are fetched and
    # built from source. Emscripten ships sdl3 and sdl3_ttf as ports but not
    # sdl3_image, and mixing a port with a source build means two SDL3s in one
    # link. Building all three keeps one.
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    # SDL_image's own stb backend decodes PNG and JPEG, which is everything in
    # assets/. Photos decode in the browser. Vendoring would clone libpng,
    # libjpeg, libtiff, dav1d and the rest to build decoders nothing here asks
    # for.
    set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_JPG ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)
    # Only what the shipped art actually needs.
    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
    set(JSON_BuildTests OFF CACHE INTERNAL "")

    FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                              GIT_TAG release-3.2.24 GIT_SHALLOW TRUE)
    FetchContent_Declare(SDL3_image GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
                              GIT_TAG release-3.2.4 GIT_SHALLOW TRUE)
    FetchContent_Declare(SDL3_ttf GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
                              GIT_TAG release-3.2.2 GIT_SHALLOW TRUE)
    FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git
                              GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_ttf nlohmann_json)
elseif(ANDROID)
    # SDL's own aar archives carry prefab modules, so the same find_package the
    # desktop uses works here too. android/fetch-deps.sh puts them in app/libs.
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    # No SDL_ttf: android.graphics.Paint draws the text, which also keeps the
    # one library in the apk that is not aligned for a 16 KB page out of it.
    # No libjpeg, because cropping a region is BitmapRegionDecoder's job here.
    # nlohmann/json is a header, so it comes from source as it does on the web.
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git
                              GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
elseif(IOS)
    # Cross-compiled from Linux with ios/xtool.toolchain.cmake, or built as an
    # Xcode project on a Mac. There is no package manager for an iPhone, so the
    # libraries are built from source as they are for the web, and linked in
    # statically: an app may not carry loose dylibs.
    #
    # No libjpeg: ImageIO decodes
    # for SDL_image, and a zoomed photo stays on its screennail as it does in
    # the browser.
    #
    # Objective-C is enabled here rather than left to SDL_image. The Xcode
    # generator decides a source file's type from the languages the top level
    # knows, and without this it compiles IMG_ImageIO.m as C.
    enable_language(OBJC)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    # SDL_image and SDL_ttf build shared by default and then look for SDL's
    # shared library, which this build does not make.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDLTTF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
    set(JSON_BuildTests OFF CACHE INTERNAL "")

    # The versions the Android build takes. SDL_ttf has no 3.4 release.
    FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                              GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
    FetchContent_Declare(SDL3_image GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
                              GIT_TAG release-3.4.6 GIT_SHALLOW TRUE)
    FetchContent_Declare(SDL3_ttf GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
                              GIT_TAG release-3.2.2 GIT_SHALLOW TRUE)
    FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git
                              GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_ttf nlohmann_json)
    # SDL passes -x objective-c on each of its own .m files for the same reason;
    # SDL_image does not, and Xcode would otherwise hand this one to clang as C.
    set_property(SOURCE "${sdl3_image_SOURCE_DIR}/src/IMG_ImageIO.m"
        DIRECTORY "${sdl3_image_SOURCE_DIR}"
        APPEND_STRING PROPERTY COMPILE_FLAGS " -x objective-c")
else()
    find_package(SDL3 CONFIG REQUIRED)
    # Windows decodes images through WIC and draws text through DirectWrite, so
    # SDL_image, SDL_ttf and libjpeg are the other desktops' alone.
    if(NOT WIN32)
        find_package(SDL3_image CONFIG REQUIRED)
        find_package(SDL3_ttf CONFIG REQUIRED)
        # The region decoder crops a tile out of a large JPEG without decoding
        # the rest, which SDL_image cannot do. SDL_image already links this, so
        # asking for the headers adds no library.
        find_package(JPEG REQUIRED)
        # LittleCMS converts the colour profile a JPEG embeds to sRGB. Distro
        # packages ship a pkg-config file for it and no CMake package.
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
    endif()
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

# The same warning level on every compiler, so a warning that appears on one
# platform is not a surprise on the next.
function(gallery3d_add_warnings target)
    if(MSVC)
        # 4244 and 4267 are narrowing conversions, which the port does
        # constantly with float and int sizes.
        target_compile_options(${target} PRIVATE /W4 /wd4244 /wd4267)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

# Symbols for the crash handler's stack traces. dbghelp turns return addresses
# into names, and without /Zi plus /DEBUG there are no names to find: Release
# drops them by default, which is exactly when a trace is hardest to get any
# other way. /Zi goes on whatever compiles, /DEBUG on whatever links.
function(gallery3d_add_symbols target)
    if(NOT MSVC)
        return()
    endif()
    target_compile_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/Zi>)
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/DEBUG>
                                              $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
                                              $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
    endif()
endfunction()

# Everything but the entry point, compiled once and linked into both the app
# and the tests. Each directory under src/ adds its own files to it, and an
# include names a file by its path from src/.
add_library(gallery3d_core OBJECT)
target_include_directories(gallery3d_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(gallery3d_core PUBLIC
    SDL3::SDL3
    nlohmann_json::nlohmann_json)
gallery3d_add_warnings(gallery3d_core)
gallery3d_add_symbols(gallery3d_core)
if(WIN32)
    # Windows 7 is the oldest Windows this runs on. The SDK then hides anything
    # newer, so a call Windows 7 lacks fails to compile instead of stopping the
    # program from starting, and code that wants a newer call looks it up at
    # run time.
    target_compile_definitions(gallery3d_core PUBLIC _WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

if(ANDROID)
    # Android has no process to start. SDLActivity loads this and calls the
    # main() inside it, and it looks for the library by the name below.
    add_library(gallery3d SHARED)
    set_target_properties(gallery3d PROPERTIES OUTPUT_NAME "main")
elseif(IOS)
    # An app bundle: the executable, its Info.plist and, after the post-build
    # copy below, assets/ beside them. xtool signs it when it installs it.
    add_executable(gallery3d MACOSX_BUNDLE)
    set_target_properties(gallery3d PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "me.mariotaku.gallery3d"
        MACOSX_BUNDLE_BUNDLE_NAME "Gallery"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1")
else()
    add_executable(gallery3d)
endif()
target_link_libraries(gallery3d PRIVATE gallery3d_core)
gallery3d_add_warnings(gallery3d)
gallery3d_add_symbols(gallery3d)

add_subdirectory(src)

if(EMSCRIPTEN)
    # The browser owns the frame clock and the canvas, so most of this is
    # telling Emscripten what the app expects to find.
    set_target_properties(gallery3d PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
    target_link_options(gallery3d PRIVATE
        # WebGL 2, which is where ES 3.0 lives. The renderer asks for an ES 2.0
        # context and uses ES 3 features where it finds them, exactly as on the
        # desktop.
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        # The wall's textures are the bulk of the memory and there is no
        # knowing how many up front.
        -sALLOW_MEMORY_GROWTH=1
        # No threads. Decoding goes through the browser and answers in
        # callbacks, so there is nothing here that would block a thread if it
        # had one. That also means the page needs no COOP and COEP headers and
        # will run from any static host.
        -sEXIT_RUNTIME=0
        # The art and the fonts, baked into a file the runtime mounts.
        --preload-file "${CMAKE_SOURCE_DIR}/assets@/assets"
        --shell-file "${CMAKE_SOURCE_DIR}/tools/web/shell.html")
elseif(ANDROID)
    # Nothing to copy. Gradle packs assets/ into the apk, and SDL's file
    # functions read that through the asset manager.
else()
    # Assets live next to the binary so the app can find them when run from
    # anywhere.
    add_custom_command(TARGET gallery3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:gallery3d>/assets")
endif()

# Desktop only. The tests are a console binary that runs to completion, which
# is neither the browser's shape nor a phone's.
if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS)
    enable_testing()
    add_subdirectory(tests)
endif()
