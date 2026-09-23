// No way to ask this platform where its interface font is. TextBackend falls
// back to the face the app ships and then to the paths it knows.
#include "graphics/SystemFont.h"

namespace SystemFont {

std::string path(bool bold) {
    (void)bold;
    return std::string();
}

}  // namespace SystemFont
