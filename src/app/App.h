// Port of com.cooliris.app.App and com.cooliris.app.Res.
#pragma once

#include <string>

namespace App {

// Wall layout density: display scale times CONTENT_SCALE, set by main().
extern float PIXEL_DENSITY;

// Chrome density: display scale alone.
extern float UI_DENSITY;

// Scales the wall's 320x480 handset layout, excluding chrome. Set by wall.scale.
extern float CONTENT_SCALE;

// Maximum screennail edge, set from the window by main(). Uploads are padded to powers of two.
extern int SCREEN_NAIL_MAX_EDGE;

// Ceiling on a grid thumbnail's texture edge, set by wall.thumbnail-max.
// Thumbnails are otherwise sized from the density, which on a dense screen asks
// for more than a slow or small-memory device can upload without the wall
// stopping. Always a power of two; zero leaves the density's own answer alone.
extern int THUMBNAIL_MAX_EDGE;

// Maximum zoom texture edge. Only the focused item retains this texture.
extern int HI_RES_MAX_EDGE;

// Backdrop blur kernel.
enum BackdropBlur {
    // One nine-tap box pass per axis.
    BACKDROP_BLUR_BOX = 0,
    // Gaussian blur with smooth falloff.
    BACKDROP_BLUR_GAUSSIAN = 1,
};

extern int BACKDROP_BLUR;

// Gaussian standard deviation in cropped-photo pixels; defaults to the nine-tap box's spread.
extern float BACKDROP_BLUR_SIGMA;

// SDL safe-area insets in pixels. HUD controls stay inside; wall content remains edge to edge.
// Desktop insets are normally zero.
struct SafeAreaInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

extern SafeAreaInsets SAFE_AREA;

// Whether the insets are a margin rather than something in the way. A TV's
// overscan is a margin: the panel trims the edges of the picture, so what
// matters must not sit near one, but nothing is covering the screen. A display
// cutout or a system bar is the other kind, and art drawn to run to the edge
// has to move aside for it. Art that bleeds may keep bleeding past a margin.
extern bool SAFE_AREA_IS_MARGIN;

// Directory that holds assets/drawable and assets/fonts. Set once at startup.
// Android packs only the fonts there. Its drawables are resources, which
// DrawableLoad reads.
extern std::string ASSET_ROOT;

// Joins a path under the asset root. The root is empty on Android, where the
// apk's own assets folder is the root, and joining that by hand would leave a
// leading slash that reads as an absolute path instead.
std::string assetPath(const std::string &relative);

// Drawable path and the density its bucket was rendered for. The plain
// drawable folder counts as 1x.
struct Drawable {
    std::string path;
    float density = 1.0f;
};

// Pass false for baseline art drawn at its native pixel dimensions.
Drawable findDrawable(const std::string &name, bool allowHigherDensity = true);

// The density of the bucket the current UI_DENSITY selects, ignoring whether
// any particular file exists in it. For reporting what the loader settled on.
float drawableBucketDensity();

// The path alone, for callers that resample to a size of their own choosing
// and so only want the best source available.
std::string drawablePath(const std::string &name);

// A chrome length in whole pixels at UI_DENSITY. tools/art/render.py rounds the
// chrome's sizes the same way, so art drawn into a box of this size lands on it
// pixel for pixel.
inline int uiPixels(float dp) {
    return (int)(dp * UI_DENSITY + 0.5f);
}

}  // namespace App

// Drawable names, replacing the generated R.drawable integers.
namespace Res {
namespace drawable {
const char *const stack_frame = "stack_frame";
const char *const grid_frame = "grid_frame";
const char *const stack_frame_focus = "stack_frame_focus";
const char *const stack_frame_gold = "stack_frame_gold";
const char *const btn_location_filter_unscaled = "btn_location_filter_unscaled";
const char *const videooverlay = "videooverlay";
const char *const grid_check_on = "grid_check_on";
const char *const grid_check_off = "grid_check_off";
const char *const icon_camera_small_unscaled = "icon_camera_small_unscaled";
const char *const icon_picasa_small_unscaled = "icon_picasa_small_unscaled";
const char *const icon_folder_small_unscaled = "icon_folder_small_unscaled";
const char *const icon_camera_small = "icon_camera_small";
const char *const icon_picasa_small = "icon_picasa_small";
const char *const icon_folder_small = "icon_folder_small";
const char *const icon_home_small = "icon_home_small";
const char *const icon_location_small = "icon_location_small";
const char *const ic_fs_details = "ic_fs_details";
const char *const transparent = "transparent";
const char *const grid_placeholder = "grid_placeholder";
const char *const grid_broken = "grid_broken";
const char *const default_background = "default_background";
}  // namespace drawable

namespace string {
const char *const no_items = "No photos here";
const char *const app_name = "Gallery";
}  // namespace string
}  // namespace Res
