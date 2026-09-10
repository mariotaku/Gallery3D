cmake_minimum_required(VERSION 3.21)
project(gallery3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

file(GLOB GALLERY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(gallery3d ${GALLERY_SOURCES})

target_include_directories(gallery3d PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(gallery3d PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf)

if(MSVC)
    target_compile_options(gallery3d PRIVATE /W3 /wd4244 /wd4267)
    target_compile_definitions(gallery3d PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Assets live next to the binary so the app can find them when run from anywhere.
add_custom_command(TARGET gallery3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:gallery3d>/assets")
