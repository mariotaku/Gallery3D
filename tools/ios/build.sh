#!/usr/bin/env bash
# Builds the iOS app on Linux (or WSL) and, given `install`, puts it on the
# iPhone connected over USB. Bash, not sh: swiftly's env.sh is written for it.
#
#   bash tools/ios/build.sh            build gallery3d.app
#   bash tools/ios/build.sh install    build, then sign and install with xtool
#
# Needs `xtool setup` done once, with an Xcode.xip, so that `swift sdk list`
# shows darwin. See ios/xtool.toolchain.cmake for what is read from it.
#
# The build tree defaults to the Linux home rather than the checkout. From WSL
# the checkout is usually on the Windows drive, where fetching and building
# SDL's thousands of files is many times slower.
set -e

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${BUILD_DIR:-$HOME/.cache/gallery3d/build-ios}"
config="${CONFIG:-Release}"

swiftly_env="${SWIFTLY_HOME_DIR:-$HOME/.local/share/swiftly}/env.sh"
if [ -f "$swiftly_env" ]; then
    . "$swiftly_env"
fi
export GALLERY3D_IOS_TOOLCHAIN="$root/ios/xtool.toolchain.cmake"

cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/ios/vcpkg-xtool.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE="$config"
cmake --build "$build"

app="$build/gallery3d.app"
echo "Built $app"

if [ "$1" = "install" ]; then
    xtool install "$app"
fi
