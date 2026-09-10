#include "App.h"

namespace App {

float PIXEL_DENSITY = 1.0f;
std::string ASSET_ROOT = "assets";

std::string drawablePath(const std::string &name) {
    return ASSET_ROOT + "/drawable/" + name + ".png";
}

}  // namespace App
