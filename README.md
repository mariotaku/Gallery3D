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

`fetch-deps.sh` downloads SDL's official Android archives, which are not on
Maven Central. They carry prefab modules, so `find_package(SDL3 CONFIG)` finds
them the same way it finds vcpkg's copy. minSdk is 24 and the build is
arm64-v8a. The catalogue and the region decoder are not wired up there yet.

The vcpkg manifest supplies SDL3, SDL3_image (PNG, JPEG, WebP, TIFF), SDL3_ttf
and other dependencies. Assets are copied next to the binary. Tests run without a window.
To build and run the Debug tests directly:

```sh
cmake --build build --config Debug --target gallery3d gallery3d_tests
./build/Debug/gallery3d_tests.exe
```
