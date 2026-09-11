// Port of com.cooliris.app.App and com.cooliris.app.Res.
//
// The Android build scaled every layout constant by the display density. The
// port keeps the same knob so the ported arithmetic stays byte for byte the
// same; main() sets it at startup.
#pragma once

#include <string>

namespace App {

// The one scale every ported layout constant multiplies by: grid item size,
// slot spacing, label size, quad size, thumbnail resolution. main() sets it to
// the display scale times CONTENT_SCALE.
extern float PIXEL_DENSITY;

// What the chrome scales by: the display scale alone, without CONTENT_SCALE.
//
// A button wants to be the right size for the screen. The wall wants to be
// bigger than the phone it was laid out for. Those are different wishes, and
// multiplying them together gives a phone sized touch target enlarged again,
// which is how the zoom buttons ended up 173 pixels wide.
extern float UI_DENSITY;

// How much bigger the wall is than the phone it was drawn for. The ported
// constants come from a 320x480 handset, so on a monitor they leave the wall as
// a small cluster in the middle of the backdrop. This factor stretches the
// whole wall at once, and it deliberately does not reach the chrome. --scale
// overrides it.
extern float CONTENT_SCALE;

// Longest edge a fullscreen photo is decoded to. The original capped this at
// 1024 because that was generous for a handset; here the window is usually
// wider than that, so a photo would be upscaled before it was even zoomed.
// main() sets it from the window, and the texture is padded to a power of two
// on top, so raising it past 1024 costs the next power of two either way.
extern int SCREEN_NAIL_MAX_EDGE;

// Longest edge for the texture behind a zoomed photo. Only the focused item
// ever holds one and the draw code drops it as focus moves, so it can afford to
// be larger than the screennail.
extern int HI_RES_MAX_EDGE;

// The part of the window it is safe to put controls in, as insets in pixels
// from each edge.
//
// The original had no notion of this: in 2009 a phone screen was a rectangle
// and all of it was yours. Now the top of a display can be a cutout and the
// bottom a home indicator, so anything you must be able to touch has to stay
// inside this while the picture behind it still runs edge to edge. That split
// is the whole idea, and it is why only the HUD reads this and the wall does
// not.
//
// SDL reports it per window. On a desktop that is the whole client area, so
// these are all zero and nothing moves.
struct SafeAreaInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

extern SafeAreaInsets SAFE_AREA;

// Directory that holds assets/drawable and assets/fonts. Set once at startup.
extern std::string ASSET_ROOT;

// A drawable, and the density its art was drawn for. Android picked a folder
// per screen density and the port does the same: the baseline folder is drawn
// for density 1, drawable-hdpi for 1.5. Picking the closer one beats scaling
// the baseline up, which is what made the breadcrumb icons stair step.
struct Drawable {
    std::string path;
    float density = 1.0f;
};

// Pass false to stay on the baseline art, for callers that draw it at its own
// size and were written against those pixel dimensions.
Drawable findDrawable(const std::string &name, bool allowHigherDensity = true);

// The density of the bucket the current PIXEL_DENSITY selects, ignoring whether
// any particular file exists in it. For reporting what the loader settled on.
float drawableBucketDensity();

// The path alone, for callers that resample to a size of their own choosing
// and so only want the best source available.
std::string drawablePath(const std::string &name);

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
const char *const default_background = "default_background";
}  // namespace drawable

namespace string {
const char *const no_items = "No photos here";
const char *const app_name = "Gallery";
}  // namespace string
}  // namespace Res
