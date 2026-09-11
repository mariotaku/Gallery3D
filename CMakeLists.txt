cmake_minimum_required(VERSION 3.21)
project(gallery3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Both targets want the same crash reporting, so it is set up in one place.
function(gallery3d_add_backtrace target)
    # Crash handler. dbghelp turns return addresses into names, and without /Zi plus
    # /DEBUG there are no names to find - Release drops them by default, which is
    # exactly when a stack trace is hardest to get any other way.
    if(WIN32)
        target_link_libraries(${target} PRIVATE dbghelp)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/Zi>)
        target_link_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/DEBUG>
                                              $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
                                              $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
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
    CURL::libcurl
    nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(gallery3d PRIVATE /W4 /wd4244 /wd4267)
    target_compile_definitions(gallery3d PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
gallery3d_add_backtrace(gallery3d)

# Assets live next to the binary so the app can find them when run from anywhere.
add_custom_command(TARGET gallery3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:gallery3d>/assets")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
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
    nlohmann_json::nlohmann_json)
target_compile_definitions(gallery3d_tests PRIVATE
    GALLERY3D_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets")

if(MSVC)
    target_compile_options(gallery3d_tests PRIVATE /W4 /wd4244 /wd4267)
    target_compile_definitions(gallery3d_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
gallery3d_add_backtrace(gallery3d_tests)

add_test(NAME gallery3d_tests COMMAND gallery3d_tests)
