// The file a platform keeps its interface font in, for the platforms that can
// name one. TextBackend opens it rather than the face the app ships, so the
// wall reads in the same type as everything else on the machine.
#pragma once

#include <string>

namespace SystemFont {

// The path of the platform's interface font, bold or regular, or an empty
// string where there is none to ask for. A platform that draws its own text
// never calls this.
std::string path(bool bold);

}  // namespace SystemFont
