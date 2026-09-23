#!/bin/sh
# Builds gallery3d for an LG webOS TV and, given `package` or `install`, makes
# an ipk from it and puts it on the TV.
#
#   sh tools/webos/build.sh            build the binary
#   sh tools/webos/build.sh package    build, then make the ipk
#   sh tools/webos/build.sh install    build, package, then install on the TV
#
# Needs the openlgtv buildroot SDK in WEBOS_SDK and a bootstrapped vcpkg in
# VCPKG_ROOT. `install` also needs ares-install and a TV set up in ares.
#
# The build tree defaults to the Linux home rather than the checkout. From WSL
# the checkout is usually on the Windows drive, where fetching and building
# SDL_image's thousands of files is many times slower.
set -e

: "${WEBOS_SDK:=/opt/arm-webos-linux-gnueabi_sdk-buildroot}"
: "${BUILD_DIR:=$HOME/.cache/gallery3d/build-webos}"
: "${STAGE_DIR:=$HOME/.cache/gallery3d/webos-sysroot}"
export WEBOS_SDK

if [ ! -d "$WEBOS_SDK" ]; then
    echo "No buildroot SDK at $WEBOS_SDK. Set WEBOS_SDK." >&2
    exit 1
fi
if [ -z "$VCPKG_ROOT" ]; then
    echo "Set VCPKG_ROOT to a bootstrapped vcpkg checkout." >&2
    exit 1
fi

root="$(cd "$(dirname "$0")/../.." && pwd)"

sh "$root/tools/webos/fetch-deps.sh" "$STAGE_DIR"

cmake -S "$root" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$root/tools/webos/vcpkg-webos.toolchain.cmake" \
    -DGALLERY3D_WEBOS_PREFIX="$STAGE_DIR"
cmake --build "$BUILD_DIR"

echo "built $BUILD_DIR/gallery3d"

if [ "$1" != "package" ] && [ "$1" != "install" ]; then
    exit 0
fi

# The ipk's root. The binary looks for SDL3 in lib/ through its rpath, and for
# the PNGs and the font in assets/, which the build already copied beside it.
app="$BUILD_DIR/ipk-root"
rm -rf "$app"
mkdir -p "$app/lib"
cp "$BUILD_DIR/gallery3d" "$app/"
cp -r "$BUILD_DIR/assets" "$app/"
# A TV carries the font its own interface is drawn in, and the wall takes that
# one, so the shipped face is weight the package does not need.
rm -rf "$app/assets/fonts"
cp "$root/webos/appinfo.json" "$app/"
cp "$root/webos/icon.png" "$root/webos/largeIcon.png" "$app/"
# SDL is linked into the binary, so the one library left to carry is libjpeg:
# a TV's is the 6b ABI (libjpeg.so.62) while the SDK builds against
# libjpeg-turbo's libjpeg.so.8. The versioned name is the one the binary's
# DT_NEEDED asks for. Everything else resolves against the firmware.
cp -P "$WEBOS_SDK/arm-webos-linux-gnueabi/sysroot/usr/lib"/libjpeg.so.8* "$app/lib/"
"$WEBOS_SDK/bin/arm-webos-linux-gnueabi-strip" "$app/gallery3d"
find "$app/lib" -type f -name '*.so.*' -exec \
    "$WEBOS_SDK/bin/arm-webos-linux-gnueabi-strip" {} +

rm -f "$BUILD_DIR"/*.ipk
ares-package -o "$BUILD_DIR" "$app"
# ares-package names the file after the architecture it finds in the binary, so
# match it rather than spelling it out.
ipk="$(ls "$BUILD_DIR"/*.ipk)"
echo "packaged $ipk"

if [ "$1" = "install" ]; then
    ares-install "$ipk"
fi
