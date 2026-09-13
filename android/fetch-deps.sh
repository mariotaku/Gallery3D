#!/bin/sh
# Fetches SDL's official Android archives into app/libs.
#
# They are not on Maven Central, so Gradle cannot resolve them by coordinate.
# Each one carries prefab modules, which is what lets CMake find SDL on Android
# with the same find_package call the desktop build uses, and the
# org.libsdl.app Java classes that MainActivity extends.
#
# Newer than the versions CMakeLists pins for the web build, and deliberately:
# these are the first releases built for a 16 KB page. A library that is not
# makes the whole apk one that a device with 16 KB pages will not load, and
# every Pixel from the 8 onwards can be put in that mode.
#
# No SDL_ttf: android.graphics.Paint draws the text here, which also draws the
# system's own font and falls back for glyphs it has no answer for. Its
# releases are also still built for a 4 KB page, and one library that is makes
# the whole apk one that a 16 KB page device will not load.
set -e

SDL_VERSION=3.4.16
SDL_IMAGE_VERSION=3.4.6

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

ls -la "$libs"
