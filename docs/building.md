# Building

Run commands from the repository root unless noted otherwise.

Requires CMake 3.21+ and a C++17 compiler. Graphics require OpenGL ES 2.0 or
the fallback desktop OpenGL 2.1 compatibility context.

[CI](../.github/workflows/build.yml) builds Linux, macOS, Windows, Android,
iOS and webOS. Tests run on Linux, macOS, Windows and an Android emulator,
with additional sanitizer checks on Linux. Windows, Android, iOS and webOS
packages are available as build artifacts; the iOS app is unsigned.

Beyond CI, the app is run by hand on Windows, on Linux, on Android in an
emulator, and on two webOS TVs: an LK5900 on webOS 4.4 and a UP7560 on
webOS 6.5.

All native builds use vcpkg for GLAD. Set `VCPKG_ROOT` to a bootstrapped vcpkg
checkout before configuring. Windows takes its remaining dependencies from the
same manifest:

```sh
cmake --preset default
cmake --build build --config Release
ctest --test-dir build -C Release
```

Linux takes everything but GLAD from the distro, so it needs one that packages
SDL3 — Debian 13 or newer, or an equivalent. vcpkg generates GLAD with python3:

```sh
sudo apt install build-essential cmake ninja-build pkg-config python3 \
    libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev \
    libjpeg-dev liblcms2-dev libfontconfig1-dev nlohmann-json3-dev zlib1g-dev
cmake --preset linux
cmake --build build
ctest --test-dir build
```

Android builds the same CMakeLists through Gradle. It needs the SDK, an NDK, a
JDK and `VCPKG_ROOT`, then:

```sh
sh android/fetch-deps.sh
cd android && ./gradlew installDebug
```

Measure with `installRelease`, never with the debug build. Gradle builds the
native code at `-O0` for the debug variant, which is slow enough to invent
problems that are not there: composing the path bar's texture takes 260ms in a
debug build and 5.6ms in a release one. The release build is signed with the
debug key so it installs without a keystore, which has to change before the apk
goes anywhere.

`fetch-deps.sh` downloads SDL's official Android archives, which are not on
Maven Central. They carry prefab modules for `find_package(SDL3 CONFIG)`;
vcpkg builds GLAD independently for the arm64-v8a and x86_64 Android triplets.
minSdk is 24.

On a Mac with Xcode, iOS is an ordinary Xcode project, unsigned. It also needs
`VCPKG_ROOT` so vcpkg can build GLAD for `arm64-ios`:

```sh
cmake --preset ios
cmake --build --preset ios
```

Without a Mac, iOS cross-compiles on Linux or WSL with [xtool](https://xtool.sh), which
provides the SDK, signs the app and installs it. Everything below runs inside
Linux; on Windows only the USB passthrough lives outside it.

1. Install the Swift 6.3 toolchain with swiftly, `xtool` 1.19.2 from its
   AppImage into `~/.local/bin`, and `usbmuxd` and `libimobiledevice-utils`
   from apt.
2. Download Xcode 26 from developer.apple.com into Linux, then run
   `xtool setup`: it logs in to an Apple ID and extracts the iOS SDK from the
   `.xip`.
3. On Windows, install [usbipd-win](https://learn.microsoft.com/windows/wsl/connect-usb)
   and attach the iPhone to WSL with `usbipd attach --wsl --busid <id>`.
   `ideviceinfo` in WSL should then name the phone.

```sh
bash tools/ios/build.sh           # build gallery3d.app
bash tools/ios/build.sh install   # build, sign and install on the phone
```

SDL, SDL_image and SDL_ttf are built from source and linked statically. GLAD
comes from vcpkg, so the Linux/WSL cross-build also needs `VCPKG_ROOT`. The
build tree is `~/.cache/gallery3d/build-ios` unless `BUILD_DIR` says
otherwise, because building on the Windows drive from WSL is many times slower.
Photos are read from the app's Documents folder, which the Files app shows.
A zoomed photo stays on its screennail there.

webOS cross-compiles on Linux or WSL with the openlgtv buildroot SDK, which
unpacks into `/opt/arm-webos-linux-gnueabi_sdk-buildroot`. It needs
`VCPKG_ROOT`, and `WEBOS_SDK` if the SDK is elsewhere:

```sh
sh tools/webos/build.sh            # build the binary
sh tools/webos/build.sh package    # build, then make the ipk
sh tools/webos/build.sh install    # also install on the TV with ares-install
```

The SDK's sysroot carries SDL2, and upstream SDL3 has no webOS video driver, so
SDL3 is the [SDL-webOS](https://github.com/webosbrew/SDL-webOS) build that
`tools/webos/fetch-deps.sh` downloads. It links static, so the ipk carries no
SDL. SDL_image and SDL_ttf have no webOS release and build from source against
it, static too. libjpeg, libpng, freetype and zlib come from the sysroot;
LittleCMS and GLAD from vcpkg, because buildroot packages neither. The ipk
carries libjpeg in `lib/`, which the binary finds through an `$ORIGIN/lib`
rpath: a TV has the 6b ABI, `libjpeg.so.62`, and the SDK builds against
libjpeg-turbo's `libjpeg.so.8`.

libstdc++ and libgcc link statically. The SDK's GCC is far newer than the one a
TV's firmware was built with, and a dynamic link asks for a GLIBCXX version the
TV does not have. The build tree is `~/.cache/gallery3d/build-webos` unless
`BUILD_DIR` says otherwise, for the same reason the iOS one is.

The wall asks `com.webos.service.attachedstoragemanager` which volumes the TV
has attached and browses those, as it browses the Pictures library on Windows.
A USB drive mounts under `/tmp/usb`. The TV's own media database lists the
pictures themselves, and the TV's gallery reads it, but db8 grants those
records to `com.lge`, `com.palm` and `com.webos` callers and refuses everyone
else, so a scan walks the folders instead. An app also runs in a jail that
reaches only `/tmp`, `/usr`, `/var` and `/media`, so a volume mounted anywhere
else is listed and reads as empty. That rules out the internal storage, whose
sample photos sit under another app's directory. `library.photos` names a
directory instead.

A TV passes no options on the command line. SAM hands a native app its launch
parameters as one JSON object, which the app skips, so settings come from a
`gallery3d.ini` in the app's directory.

The window takes the size webOS gives the display, which covers the screen. A TV
hands an app 1280x720 and scales that to the panel, and even a 4K panel draws
its UI at 1080p at most, so this is the size to draw at rather than the panel's.
The window's shape has to match the panel's: a 16:10 window on a 16:9 screen is
stretched a ninth wider than it is tall, which reads as slightly wrong rather
than obviously broken.

The wall draws its text in Museo Sans, which fontconfig finds on the TV. A TV's
own `sans-serif` is the face its interface uses, and that one ships in a single
weight, so bold would come back as the regular file; Museo Sans sits beside it
with a bold to match. The package carries no font of its own.

Wall thumbnails come from the TV's thumbnail service, which is what the TV's
own gallery draws its grid from. Unlike the freedesktop.org cache, which the
Linux build only reads, the service makes a thumbnail on the first ask and
keeps it: for a picture on a drive it writes beside the pictures, as the TV
does for its own gallery. It returns a JPEG whatever it names the file, and
turns the picture upright, so the wall turns it back by the photo's EXIF tag.
Where it writes changes from one TV to the next, so the app opens the file the
service names and never spells a path out.

The jail binds `/tmp` without the mounts under it, and it takes the binds from a
namespace the TV sets up as it starts. A drive plugged in after that is listed
by the storage manager and still reads as empty, however many times the app is
restarted. Restart the TV with the drive attached.

The vcpkg manifest supplies GLAD everywhere, and SDL3, nlohmann-json and zlib
on Windows. Its `webos` feature adds LittleCMS for the TV. Windows needs no SDL_image, SDL_ttf or libjpeg: WIC decodes every
image, including HEIF and camera RAW once their extensions are installed from
the Microsoft Store, and DirectWrite draws the text. Assets are copied next to
the binary. Tests run without a window.
To build and run the Debug tests directly:

```sh
cmake --build build --config Debug --target gallery3d gallery3d_tests
./build/tests/Debug/gallery3d_tests.exe
```
