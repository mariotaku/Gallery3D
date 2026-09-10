#include "App.h"

namespace App {

float PIXEL_DENSITY = 1.0f;
// 1.5 fills the default 1280x800 window with two rows of stacks and no
// clipping. It also crosses the 1.5 threshold that DisplaySlot and
// GridDrawables use to pick the 256x64 label texture over the 128x32 one, so
// captions stay sharp at this size.
float CONTENT_SCALE = 1.5f;
std::string ASSET_ROOT = "assets";

std::string drawablePath(const std::string &name) {
    return ASSET_ROOT + "/drawable/" + name + ".png";
}

}  // namespace App
