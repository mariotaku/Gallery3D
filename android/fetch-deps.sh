#!/bin/sh
# Fetches SDL's official Android archives into app/libs.
#
# They are not on Maven Central, so Gradle cannot resolve them by coordinate.
# Each one carries prefab modules, which is what lets CMake find SDL on Android
# with the same find_package call the desktop build uses, and the
# org.libsdl.app Java classes that MainActivity extends.
#
# Versions match the ones CMakeLists pins for the web build.
set -e

SDL_VERSION=3.2.24
SDL_IMAGE_VERSION=3.2.4
SDL_TTF_VERSION=3.2.2

libs="$(dirname "$0")/app/libs"
mkdir -p "$libs"

fetch() {
    repo="$1"
    tag="$2"
    archive="$3"
    aar="$4"
    if [ -f "$libs/$aar" ]; then
        echo "have $aar"
        return
    fi
    echo "fetching $aar"
    url="https://github.com/libsdl-org/$repo/releases/download/$tag/$archive"
    temp="$(mktemp -d)"
    curl -sfL -o "$temp/package.zip" "$url"
    unzip -q -o -j "$temp/package.zip" "$aar" -d "$libs"
    rm -rf "$temp"
}

fetch SDL "release-$SDL_VERSION" \
    "SDL3-devel-$SDL_VERSION-android.zip" "SDL3-$SDL_VERSION.aar"
fetch SDL_image "release-$SDL_IMAGE_VERSION" \
    "SDL3_image-devel-$SDL_IMAGE_VERSION-android.zip" "SDL3_image-$SDL_IMAGE_VERSION.aar"
fetch SDL_ttf "release-$SDL_TTF_VERSION" \
    "SDL3_ttf-devel-$SDL_TTF_VERSION-android.zip" "SDL3_ttf-$SDL_TTF_VERSION.aar"

ls -la "$libs"
