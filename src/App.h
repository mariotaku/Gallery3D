// Port of com.cooliris.app.App and com.cooliris.app.Res.
//
// The Android build scaled every layout constant by the display density. The
// port keeps the same knob so the ported arithmetic stays byte for byte the
// same; main() sets it from the SDL display scale.
#pragma once

#include <string>

namespace App {

extern float PIXEL_DENSITY;

// Directory that holds assets/drawable and assets/fonts. Set once at startup.
extern std::string ASSET_ROOT;

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
