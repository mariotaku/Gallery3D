// Where iOS keeps the font it draws its own interface in.
//
// An app may not reach into /System/Library/Fonts by a path it spelled out, and
// the names there change between releases, so a list of guesses is no good.
// CoreText answers with the file the system font actually lives in, and hands
// back a URL the app is allowed to read.
#include "graphics/SystemFont.h"

#include <CoreText/CoreText.h>
#include <Foundation/Foundation.h>

namespace SystemFont {

std::string path(bool bold) {
    // The size is required and does not matter: the file is the same whatever
    // size it is asked for.
    CTFontUIFontType role = bold ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem;
    CTFontRef font = CTFontCreateUIFontForLanguage(role, 20.0, nullptr);
    if (font == nullptr) {
        return std::string();
    }
    CFURLRef url = (CFURLRef)CTFontCopyAttribute(font, kCTFontURLAttribute);
    CFRelease(font);
    if (url == nullptr) {
        return std::string();
    }
    std::string result;
    // The system font is a collection on iOS, one file holding every weight.
    // The URL names the file; TTF_OpenFont reads the first face in it, which is
    // the regular one, so bold comes back as the same file and is drawn by the
    // caller thickening it.
    if (NSString *file = [(__bridge NSURL *)url path]) {
        result = [file UTF8String];
    }
    CFRelease(url);
    return result;
}

}  // namespace SystemFont
