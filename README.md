## What it is

Gallery3D SDL is a C++17 / SDL3 port of the Cooliris 3D photo wall from AOSP
`platform/packages/apps/Gallery3D`, tag `android-2.3.7_r1`. Browse folder stacks,
open albums, and zoom photos. Java reference sources are in `reference/`.
Apache 2.0, Copyright 2009 The Android Open Source Project; see `LICENSE`.

![album view](docs/albums.png)
![grid view](docs/grid.png)

## Usage

```sh
build/Release/gallery3d.exe
gallery3d --help
```

The app scans your Pictures directory, with one album per folder containing images.
`--help` lists all settings and run options. Videos open in the desktop's default
player. Delete uses the recycle bin on Windows and the freedesktop.org trash on
Linux. Rotation persists when an EXIF orientation tag can be rewritten. Crop,
wallpaper, share, Picasa sync and reverse geocoding are unavailable; location
controls remain hidden without place names.

Settings precedence is compiled defaults, then INI file, then environment.
The app searches for `gallery3d.ini` in the working directory, then its platform
config directory. `--config PATH` or `GALLERY3D_CONFIG` selects a file explicitly;
an unreadable explicit file is an error. Startup logs settings and their sources,
and reports unknown keys with file and line numbers. Settings have no flag form.

```ini
[library]
photos = D:/Pictures
artic = no

[wall]
scale = 1.5

[backdrop]
blur = gaussian
sigma = 4.0
```

Environment names follow the setting: `backdrop.sigma` becomes
`GALLERY3D_BACKDROP_SIGMA`.

- `library.also` adds a second photo directory; `library.artic` enables the
  read-only Art Institute of Chicago catalogue.
- `wall.scale` scales stacks, spacing, captions and thumbnail resolution.
  Display scaling applies separately; controls follow display scale alone.
- `wall.thumbnail-max` caps a grid thumbnail's texture edge, as a power of two.
  The size otherwise comes from the display density, which on a dense screen can
  ask for more than a slow or small-memory device uploads without the wall
  stopping. 256 is the smallest worth trying; unset, the density decides.
- `window.safe-area` simulates cutout insets as `left,top,right,bottom`.
  Controls stay inside the safe area; photos and backdrop fill the window.
- `backdrop.blur` accepts `gaussian` (default) or `box`. `backdrop.sigma` controls
  gaussian softness in cropped-photo pixels; its default is 2.58.
- Web settings use URL parameters, including `?blur=box&sigma=6&scale=1.2`.

| Input | Action |
| --- | --- |
| Drag / flick | Scroll / fling the wall |
| Click a stack | Open album |
| Click a photo | Fullscreen |
| Double click | Toggle fullscreen-photo zoom |
| Wheel | Spread a stack or zoom a fullscreen photo |
| Arrow keys | Move focus |
| Enter / space | Open |
| Esc / backspace | Back; quit at the top level |

The minimum window size is 320x320 display units. On Windows, content extends
under app-drawn caption buttons while the system retains snapping and resize borders.
On Android the wall runs under the status and navigation bars, which stay visible
but transparent; controls keep clear of them the same way they keep clear of a
display cutout.

A photo is decoded straight to the size it is wanted at, by libjpeg-turbo on the
desktop and BitmapFactory on Android, rather than being built at full size and
scaled after. Formats with no reducing decoder behind them take the whole-image
path.

Zoomed images load only the tiles on screen. The museum crops on its server. A
local JPEG is cropped by libjpeg-turbo on the desktop and by BitmapRegionDecoder
on Android, either way reading the photo once rather than once per tile. Formats
with no region decoder behind them stay on one downscaled decode.

For scripted screenshots:

```sh
gallery3d --screenshot out.png --frames 400
gallery3d --open 3 --select --popup 1 --screenshot out.png --frames 400
gallery3d --window-size 320x320 --screenshot out.png
```

## Building

Requires CMake 3.21+ and a C++17 compiler. Windows and Linux are the validated
native platforms. Graphics require OpenGL ES 2.0 or the fallback desktop
OpenGL 2.1 compatibility context.

Windows takes its dependencies from vcpkg, with `VCPKG_ROOT` set:

```sh
cmake --preset default
cmake --build build --config Release
ctest --test-dir build -C Release
```

Linux takes them from the distro, so it needs one that packages SDL3 —
Debian 13 or newer, or an equivalent:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev \
    libcurl4-openssl-dev nlohmann-json3-dev
cmake --preset linux
cmake --build build
ctest --test-dir build
```

Android builds the same CMakeLists through Gradle. It needs the SDK, an NDK and
a JDK, then:

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
Maven Central. They carry prefab modules, so `find_package(SDL3 CONFIG)` finds
them the same way it finds vcpkg's copy. minSdk is 24 and the build is
arm64-v8a. The catalogue and the region decoder are not wired up there yet.

iOS cross-compiles on Linux or WSL with [xtool](https://xtool.sh), which
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

SDL, SDL_image and SDL_ttf are built from source and linked statically. The
build tree is `~/.cache/gallery3d/build-ios` unless `BUILD_DIR` says
otherwise, because building on the Windows drive from WSL is many times slower.
Photos are read from the app's Documents folder, which the Files app shows.
The catalogue has no network there yet, and a zoomed photo stays on its
screennail.

The vcpkg manifest supplies SDL3, SDL3_image (PNG, JPEG, WebP, TIFF), SDL3_ttf
and other dependencies. Assets are copied next to the binary. Tests run without a window.
To build and run the Debug tests directly:

```sh
cmake --build build --config Debug --target gallery3d gallery3d_tests
./build/Debug/gallery3d_tests.exe
```
