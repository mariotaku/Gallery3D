#include "media/Http.h"

#include <SDL3/SDL.h>

namespace Http {

// No curl in the NDK or the iOS SDK. Reaching the network here means going out
// through the platform, which the museum source will need before it can run on
// a phone.
bool get(const std::string &url, std::vector<uint8_t> *out) {
    SDL_Log("No network on this platform yet, cannot fetch %s", url.c_str());
    if (out != nullptr) {
        out->clear();
    }
    return false;
}

void getAsync(const std::string &url, Callback done) {
    std::vector<uint8_t> bytes;
    const bool ok = get(url, &bytes);
    if (done) {
        done(ok, std::move(bytes));
    }
}

const char *userAgent() {
    return "";
}

}  // namespace Http
