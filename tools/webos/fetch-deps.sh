#!/bin/sh
# Fetches the webOS build of SDL3 into a staging prefix.
#
# The buildroot SDK's sysroot carries SDL2, not SDL3, and upstream SDL3 has no
# webOS video driver. webosbrew/SDL-webOS publishes one built for armv7 with the
# wayland-webos driver, the remote's pointer and the TV's key handling. The
# archive is a CMake package, so find_package(SDL3 CONFIG) finds it once
# CMAKE_PREFIX_PATH names the prefix below. It carries both a static and a
# shared build; the top level links the static one, so the ipk ships no SDL.
#
# SDL_image and SDL_ttf have no webOS release, so the top level builds those
# from source against this SDL3.
set -e

SDL_VERSION=3.4.16-webos.4

prefix="${1:-$HOME/.cache/gallery3d/webos-sysroot}"
stamp="$prefix/.sdl3-version"

# The stamp names the version unpacked, so raising SDL_VERSION fetches the new
# one rather than leaving the old one in place.
if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$SDL_VERSION" ]; then
    echo "have SDL3 $SDL_VERSION in $prefix"
    exit 0
fi

url="https://github.com/webosbrew/SDL-webOS/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
echo "fetching SDL3 $SDL_VERSION"
rm -rf "$prefix"
mkdir -p "$prefix"
temp="$(mktemp -d)"
curl -sfL -o "$temp/sdl3.tar.gz" "$url"
tar xzf "$temp/sdl3.tar.gz" -C "$prefix"
rm -rf "$temp"
echo "$SDL_VERSION" > "$stamp"

echo "unpacked into $prefix"
