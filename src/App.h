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

// How much bigger the wall is than the phone it was drawn for. The ported
// constants come from a 320x480 handset, so on a monitor they leave the wall as
// a small cluster in the middle of the backdrop. This factor stretches the
// whole wall at once. --scale overrides it.
extern float CONTENT_SCALE;

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
const char *const transparent = "transparent";
const char *const grid_placeholder = "grid_placeholder";
const char *const default_background = "default_background";
}  // namespace drawable

namespace string {
const char *const no_items = "No photos here";
const char *const app_name = "Gallery";
}  // namespace string
}  // namespace Res
