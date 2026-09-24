# Usage

```sh
build/Release/gallery3d.exe
gallery3d --help
```

The app scans your Pictures directory, with one album per folder containing images.
On Windows that is the whole Pictures library, which can include folders on
other drives, and albums inside the Camera Roll get the camera icon. Cloud
placeholders kept online only, such as Dropbox or OneDrive files not on this
disk, are left off the wall: opening one to read it would download the whole
file. `library.photos` names one directory instead. On Windows the photos are
listed from the Windows Search index, as MediaStore lists them on Android. A
folder the indexer covers is not walked, so a photo copied in appears once the
indexer has reached it. Dates, sizes, orientation and position come from the
index too, and only a photo it has no size for is opened for its EXIF. Folders
outside the indexer's scope are walked and read file by file, as every folder
is on other platforms.
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

[wall]
scale = 1.5

[backdrop]
blur = gaussian
sigma = 4.0
```

Environment names follow the setting: `backdrop.sigma` becomes
`GALLERY3D_BACKDROP_SIGMA`.

- `library.also` adds a second photo directory.
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

A photo is decoded straight to the size it is wanted at, rather than being built
at full size and scaled after. WIC does that on Windows, libjpeg-turbo on the
other desktops and BitmapFactory on Android. On Windows a thumbnail stored in
the file stands in when it is big enough, and a RAW photo decodes from the
camera's embedded preview. On Linux the wall takes the thumbnail a file manager
already left in the freedesktop.org thumbnail cache, when one is big enough.
Formats with no reducing decoder behind them take the whole-image path.

Zoomed images load only the tiles on screen. A photo is cropped by WIC on
Windows, by libjpeg-turbo on the other
desktops and by BitmapRegionDecoder on Android, each reading the photo once
rather than once per tile. WIC crops JPEG, HEIF, TIFF, WebP and RAW; libjpeg
only JPEG. Formats with no region decoder behind them stay on one downscaled
decode.

The icons, buttons, bars and frames are drawn as SVG in `art/` and rendered to
the PNGs under `assets/` by `tools/art/render.py`; [art guide](../art/README.md) describes
the look they keep.

For scripted screenshots:

```sh
gallery3d --screenshot out.png --frames 400
gallery3d --open 3 --select --popup 1 --screenshot out.png --frames 400
gallery3d --window-size 320x320 --screenshot out.png
```

