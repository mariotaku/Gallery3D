cmake_minimum_required(VERSION 3.21)
project(gallery3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EMSCRIPTEN)
    # The web build has no vcpkg to ask, so the dependencies are fetched and
    # built from source. Emscripten ships sdl3 and sdl3_ttf as ports but not
    # sdl3_image, and mixing a port with a source build means two SDL3s in one
    # link. Building all three keeps one.
    #
    # No curl: a page cannot open a socket, so Http.cpp uses the Fetch API
    # instead. See Http.h.
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    # SDL_image's own stb backend decodes PNG and JPEG, which is everything the
    # museum serves and everything in assets/. Vendoring would clone libpng,
    # libjpeg, libtiff, dav1d and the rest to build decoders nothing here asks
    # for.
    set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_JPG ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)
    # Only what the museum source and the shipped art actually need.
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
else()
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    # Module mode, not CONFIG. Distro libcurl ships no CMake config package,
    # and FindCURL looks for one before falling back. Both give CURL::libcurl.
    find_package(CURL REQUIRED)
    # RegionDecoder crops a tile out of a large JPEG without decoding the rest,
    # which SDL_image cannot do. SDL_image already links this, so asking for the
    # headers adds no library.
    find_package(JPEG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

# Both targets want the same platform libraries, so they are set up in one place.
function(gallery3d_add_backtrace target)
    # Crash handler. dbghelp turns return addresses into names, and without /Zi plus
    # /DEBUG there are no names to find - Release drops them by default, which is
    # exactly when a stack trace is hardest to get any other way.
    if(WIN32)
        # dbghelp for the stack trace; dwmapi and comctl32 for the extended
        # window frame in WindowFrame.cpp.
        target_link_libraries(${target} PRIVATE dbghelp dwmapi comctl32)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/Zi>)
        target_link_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/DEBUG>
                                              $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
                                              $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
    endif()
endfunction()

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

file(GLOB GALLERY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp")

# Everything but the entry point, so the tests can link the same code.
set(GALLERY_LIB_SOURCES ${GALLERY_SOURCES})
list(REMOVE_ITEM GALLERY_LIB_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

add_executable(gallery3d ${GALLERY_SOURCES})

target_include_directories(gallery3d PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(gallery3d PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    nlohmann_json::nlohmann_json)
if(NOT EMSCRIPTEN)
    target_link_libraries(gallery3d PRIVATE CURL::libcurl JPEG::JPEG)
    target_compile_definitions(gallery3d PRIVATE GALLERY3D_HAVE_JPEG)
endif()

gallery3d_add_warnings(gallery3d)
gallery3d_add_backtrace(gallery3d)

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
        # Http.cpp talks to the museum through this.
        -sFETCH=1
        # No threads. Fetching and decoding both go through the browser and
        # answer in callbacks, so there is nothing here that would block a
        # thread if it had one. That also means the page needs no COOP and COEP
        # headers and will run from any static host.
        -sEXIT_RUNTIME=0
        # The art and the fonts, baked into a file the runtime mounts.
        --preload-file "${CMAKE_SOURCE_DIR}/assets@/assets"
        --shell-file "${CMAKE_SOURCE_DIR}/tools/web/shell.html")
else()
    # Assets live next to the binary so the app can find them when run from
    # anywhere.
    add_custom_command(TARGET gallery3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:gallery3d>/assets")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Native only. They are a console binary that runs to completion, which is not
# a shape the browser has.
if(NOT EMSCRIPTEN)
# Only the parts that hold still: decoding, geometry and layout arithmetic.
# Anything needing a GL context stays with the screenshot checks instead.
enable_testing()

file(GLOB GALLERY_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")

add_executable(gallery3d_tests ${GALLERY_TEST_SOURCES} ${GALLERY_LIB_SOURCES})
target_include_directories(gallery3d_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(gallery3d_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    CURL::libcurl
    JPEG::JPEG
    nlohmann_json::nlohmann_json)
target_compile_definitions(gallery3d_tests PRIVATE
    GALLERY3D_HAVE_JPEG
    GALLERY3D_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets")

gallery3d_add_warnings(gallery3d_tests)
gallery3d_add_backtrace(gallery3d_tests)

add_test(NAME gallery3d_tests COMMAND gallery3d_tests)
endif()
