# Gallery3D SDL

A C++ / SDL3 port of the Cooliris "Gallery3D" photo wall from Android, taken from
AOSP `platform/packages/apps/Gallery3D` at tag `android-2.3.7_r1`, the last
Gingerbread release and the final state of this package.

It browses a directory of photos as the original did: one 3D stack per folder,
tap a stack to drop into that album's grid, fling along the wall.

![album view](docs/albums.png)

## Why this port is straightforward

The original renderer targets **OpenGL ES 1.1 fixed function** (`GL11`, client
vertex arrays, `glTexEnv`, `OES_draw_texture`). All of that is gone in ES 2.0, so
`RenderView` absorbs it:

| Original | Here |
| --- | --- |
| `glMatrixMode` / `glTranslatef` / `GLU.gluLookAt` | `MatrixStack` + `Mat4` in `RenderView` |
| `glTexEnv` `GL_REPLACE` and `GL_MODULATE` | the `uColor` uniform |
| `glTexEnv` `GL_COMBINE` / `GL_INTERPOLATE` | the two texture mix program |
| `glDrawTexfOES` (`OES_draw_texture`) | an ortho projected quad whose `z` is written straight to depth |
| `glVertexPointer` / `glTexCoordPointer` | vertex attributes 0, 1 and 2 |
| `Bitmap` / `BitmapFactory` / `Canvas` | `Bitmap` on SDL_image |
| `StringTexture` via `android.graphics.Canvas` | `StringTexture` via SDL_ttf |
| `MediaStore` `ContentProvider` | `LocalDataSource`, a directory walk |
| `GLSurfaceView` render thread + `Handler` | the SDL event loop + `std::thread` loaders |

Everything above that line - the wall layout, the camera, the stack jitter, the
frame mesh, the visible range search, the display item animation - is a direct
translation. File names match the Java ones so the two can be read side by side.

## Base version

`android-2.3.7_r1` is byte identical to `android-2.3.6_r1` and to the
`gingerbread` branch head, so it really is the end of the line: Google deleted
the in-tree rewrite before 2.3_r1 with "we are doing all our rewrites in Master
(Honeycomb)", and the package was frozen after that.

The GL pipeline itself - `RenderView`, `GridDrawManager`, `GridQuad`,
`GridCamera`, the wall layout - is unchanged all the way from Froyo. What
Gingerbread did change, and what this port therefore carries, is five fixes,
each marked with a comment where it lands:

- `GridCameraManager::constrainCameraForSlot` centres instead of clamping when
  the viewport is larger than the image, so a zoomed out photo stops drifting
  into a corner.
- `GridInputProcessor::onScale` zooms about the focus point and pans by the
  focus delta, so the pixels under your fingers stay put, and it no longer
  clamps at the end of the gesture.
- `GridLayer::setZoomValue` converges at 10x, so the camera tracks a pinch
  instead of lagging behind it.
- `MediaBucketList::add` matches sets and items by reference instead of by id,
  fixing selection when two of them share an id.
- `MediaFeed::addMediaSet` drops a stale set with the same id before adding,
  so a rescan replaces an album instead of duplicating it.

Gingerbread's remaining changes do not apply here: 85% of that diff is new
locales and mdpi assets, and the rest is the Picasa HTTPS fix, a
`WallpaperManager` API migration and an `isExternalStorageRemovable` string -
all in code this port does not have.

## Build

Needs CMake 3.21+, a C++17 compiler and vcpkg (`VCPKG_ROOT` set).

```sh
cmake --preset default
cmake --build build --config Release
```

SDL3, SDL3_image (png, jpeg, webp, tiff) and SDL3_ttf come from the vcpkg
manifest. Assets are copied next to the binary at build time.

## Test

```sh
ctest --test-dir build -C Release
```

Covers the arithmetic that has no picture attached: EXIF decoding, power of two
padding, cover cropping, density bucket selection and nine patch guides. Every
one of those has been wrong at some point here and none of them announced it.
Anything needing a GL context is checked by screenshot instead, through the
debug flags on the binary.

## Run

```sh
build/Release/gallery3d.exe [photo directory] [--also directory]
```

Defaults to your Pictures folder. It walks the tree and makes one album per
folder that holds images.

`--also` shows a second directory on the same wall. It runs two data sources
through `ConcatenatedDataSource`, which is the seam another kind of storage
would plug into: each album remembers which source produced it, and the feed
asks that one for its items and for deletes and rotations.

The layout constants come from a 320x480 handset, so on a monitor the wall
would be a small cluster in the middle of the backdrop. `--scale` stretches the
whole wall - stacks, spacing, captions and thumbnail resolution together. It
defaults to 1.5, which fills a 1280x800 window. Raise it for a bigger wall with
fewer albums on screen, lower it for more.

```sh
gallery3d --scale 2.0        # bigger stacks, fewer per screen
```

The display DPI is a separate factor and still applies on top, so the wall
keeps the same apparent size on a HiDPI screen.

| Input | Action |
| --- | --- |
| drag | scroll the wall |
| flick | fling |
| click a stack | open that album |
| click a photo | fullscreen |
| double click | zoom in and out of a fullscreen photo |
| wheel | pinch: spreads a stack, zooms a fullscreen photo |
| arrow keys | move the focus |
| enter / space | open |
| esc / backspace | back, and quit from the top level |

For checking a build without a hand on the mouse:

```sh
gallery3d --screenshot out.png --frames 400        # render N frames, save the buffer
gallery3d --open 3 --screenshot out.png --frames 700  # open album 3 first
```

## Graphics context

It asks SDL for an **ES 2.0** context and falls back to a desktop GL 2.1
compatibility context if the driver refuses. The only difference is the shader
prelude (`#version 100` plus precision qualifiers, or `#version 120`); the entry
points the port uses exist in both. `src/gles2.h` declares them and loads them
through `SDL_GL_GetProcAddress`, so there is no GL loader dependency.

## What is not ported yet

The 3D grid and the HUD are done: album stacks, drill down, fling, fullscreen,
the path bar, the selection and fullscreen bars with their popup menus, and the
time bar. Timeline clustering is the real thing, delete and rotate work, and a
video opens in whatever the desktop plays it with.

Still missing, and wanted: reverse geocoding, which needs a service the port
does not have, and a build on anything other than Windows.

Deliberately not ported: the crop screen and the wallpaper service. Tilt, which
leaned the wall with the accelerometer and has no desktop equivalent. Picasa
sync, whose service is long gone, though the `DataSource` interface it
implemented is still the seam another storage backend would use. And the
`ImageManager` content provider plumbing, which is Android specific throughout.

## Licence

The original is Apache 2.0, Copyright 2009 The Android Open Source Project; see
`MODULE_LICENSE_APACHE2`. This port keeps that licence.
