// The interface font, from fontconfig, which is what every other program on a
// Linux system asks. It knows which file backs a family, which families stand
// in for one another and what the machine has installed, none of which a list
// of paths in here could keep up with.
#include "graphics/SystemFont.h"

#include <fontconfig/fontconfig.h>

#include <cstring>

namespace {

// The file fontconfig gives for a family, or an empty string. fontconfig always
// answers with its nearest match, so asking for a family the machine does not
// have returns something else entirely; requireFamily throws that away.
std::string matchFamily(const char *family, bool bold, bool requireFamily) {
    if (FcInit() == FcFalse) {
        return std::string();
    }
    FcPattern *pattern = FcPatternCreate();
    if (pattern == nullptr) {
        return std::string();
    }
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family));
    FcPatternAddInteger(pattern, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
    // The two calls that apply the machine's own rules to the request. Without
    // them a match comes back for the pattern as written, which is rarely what
    // the system would have drawn.
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern *matched = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    if (matched == nullptr || result != FcResultMatch) {
        if (matched != nullptr) {
            FcPatternDestroy(matched);
        }
        return std::string();
    }

    std::string file;
    FcChar8 *name = nullptr;
    const bool wanted =
        !requireFamily ||
        (FcPatternGetString(matched, FC_FAMILY, 0, &name) == FcResultMatch && name != nullptr &&
         FcStrCmpIgnoreCase(name, reinterpret_cast<const FcChar8 *>(family)) == 0);
    FcChar8 *path = nullptr;
    if (wanted && FcPatternGetString(matched, FC_FILE, 0, &path) == FcResultMatch && path != nullptr) {
        file = reinterpret_cast<const char *>(path);
    }
    FcPatternDestroy(matched);
    return file;
}

}  // namespace

namespace SystemFont {

std::string path(bool bold) {
#if defined(__WEBOS__)
    // A TV's sans-serif is the face its own interface is drawn in, and that one
    // ships in a single weight, so bold comes back as the regular file. Museo
    // Sans sits beside it with a real bold, and every TV tried had it.
    const std::string museo = matchFamily("Museo Sans", bold, true);
    if (!museo.empty()) {
        return museo;
    }
#endif
    return matchFamily("sans-serif", bold, false);
}

}  // namespace SystemFont
