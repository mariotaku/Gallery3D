#include "app/Settings.h"

#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "graphics/Bitmap.h"

namespace Settings {

namespace {

std::string trimmed(const std::string &text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (unsigned char)text[begin] <= ' ') {
        ++begin;
    }
    while (end > begin && (unsigned char)text[end - 1] <= ' ') {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string lowered(std::string text) {
    for (char &c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
    }
    return text;
}

bool isKnown(const std::string &name) {
    for (const Known &entry : known()) {
        if (name == entry.name) {
            return true;
        }
    }
    return false;
}

}  // namespace

const std::vector<Known> &known() {
    static const std::vector<Known> entries = {
        {"library.photos", "the directory of photos to browse, one stack per folder"},
        {"library.also", "a second directory shown on the same wall"},
        {"wall.scale", "how much bigger the wall is than the phone it was laid out for"},
        {"wall.thumbnail-max", "cap a grid thumbnail's texture edge, a power of two"},
        {"backdrop.blur", "how the wash behind the wall is blurred: gaussian or box"},
        {"backdrop.sigma", "how strong the gaussian is, 2.58 matches the box"},
        {"window.safe-area", "pretend the window has cutouts: left,top,right,bottom"},
    };
    return entries;
}

std::string environmentNameFor(const std::string &name) {
    std::string result = "GALLERY3D_";
    for (char c : name) {
        if (c == '.' || c == '-') {
            result += '_';
        } else if (c >= 'a' && c <= 'z') {
            result += (char)(c - 'a' + 'A');
        } else {
            result += c;
        }
    }
    return result;
}

void Store::readIni(const std::string &text, const std::string &describedAs) {
    std::string section;
    size_t lineStart = 0;
    int lineNumber = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = text.size();
        }
        std::string line = trimmed(text.substr(lineStart, lineEnd - lineStart));
        lineStart = lineEnd + 1;
        ++lineNumber;

        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close == std::string::npos) {
                mComplaints.push_back(describedAs + ":" + std::to_string(lineNumber) + ": unclosed section header");
                continue;
            }
            section = lowered(trimmed(line.substr(1, close - 1)));
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            mComplaints.push_back(describedAs + ":" + std::to_string(lineNumber) + ": no '=' in \"" + line + "\"");
            continue;
        }
        const std::string key = lowered(trimmed(line.substr(0, equals)));
        const std::string value = trimmed(line.substr(equals + 1));
        if (section.empty()) {
            mComplaints.push_back(describedAs + ":" + std::to_string(lineNumber) + ": " + key +
                                  " is not under any [section]");
            continue;
        }
        const std::string name = section + "." + key;
        if (!isKnown(name)) {
            // Report unknown settings.
            mComplaints.push_back(describedAs + ":" + std::to_string(lineNumber) + ": no such setting as " + name);
            continue;
        }
        if (value.empty()) {
            // An empty value means "leave the default alone", so a file can
            // list every setting with the blanks acting as documentation.
            continue;
        }
        mValues[name] = Entry{value, describedAs};
    }
}

void Store::readEnvironment(const std::function<const char *(const char *)> &lookup) {
    for (const Known &entry : known()) {
        const std::string variable = environmentNameFor(entry.name);
        const char *value = lookup(variable.c_str());
        if (value == nullptr || *value == '\0') {
            continue;
        }
        mValues[entry.name] = Entry{trimmed(value), "environment"};
    }
}

bool Store::load(const std::string &explicitPath) {
    std::vector<std::string> candidates;
    if (!explicitPath.empty()) {
        candidates.push_back(explicitPath);
    } else {
        candidates = searchPaths();
    }

    for (const std::string &candidate : candidates) {
        std::vector<uint8_t> bytes;
        if (!Bitmap::readFile(candidate, &bytes)) {
            continue;
        }
        mPath = candidate;
        readIni(std::string((const char *)bytes.data(), bytes.size()), candidate);
        break;
    }

    const bool foundWhatWasAskedFor = explicitPath.empty() || !mPath.empty();
    readEnvironment([](const char *name) { return std::getenv(name); });
    return foundWhatWasAskedFor;
}

bool Store::has(const std::string &name) const {
    return mValues.find(name) != mValues.end();
}

std::string Store::get(const std::string &name, const std::string &fallback) const {
    auto found = mValues.find(name);
    return (found == mValues.end()) ? fallback : found->second.value;
}

float Store::getFloat(const std::string &name, float fallback) const {
    auto found = mValues.find(name);
    if (found == mValues.end()) {
        return fallback;
    }
    return (float)std::atof(found->second.value.c_str());
}

bool Store::getBool(const std::string &name, bool fallback) const {
    auto found = mValues.find(name);
    if (found == mValues.end()) {
        return fallback;
    }
    const std::string value = lowered(found->second.value);
    if (value == "true" || value == "yes" || value == "on" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "off" || value == "0") {
        return false;
    }
    return fallback;
}

std::string Store::sourceOf(const std::string &name) const {
    auto found = mValues.find(name);
    return (found == mValues.end()) ? std::string() : found->second.source;
}

std::vector<std::string> searchPaths() {
    std::vector<std::string> paths;
    // Search the working directory first.
    paths.push_back("gallery3d.ini");
    // Then search the platform config directory; SDL creates it if missing.
    if (char *pref = SDL_GetPrefPath("mariotaku", "Gallery3D")) {
        paths.push_back(std::string(pref) + "gallery3d.ini");
        SDL_free(pref);
    }
    return paths;
}

}  // namespace Settings
