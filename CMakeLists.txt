cmake_minimum_required(VERSION 3.21)
project(gallery3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

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
    SDL3_ttf::SDL3_ttf)

if(MSVC)
    target_compile_options(gallery3d PRIVATE /W4 /wd4244 /wd4267)
    target_compile_definitions(gallery3d PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

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
    SDL3_ttf::SDL3_ttf)
target_compile_definitions(gallery3d_tests PRIVATE
    GALLERY3D_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets")

if(MSVC)
    target_compile_options(gallery3d_tests PRIVATE /W4 /wd4244 /wd4267)
    target_compile_definitions(gallery3d_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

add_test(NAME gallery3d_tests COMMAND gallery3d_tests)
