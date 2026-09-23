// Thumbnails from the TV's thumbnail service, which is what the TV's own
// gallery draws its grid from. The service makes one on the first ask and keeps
// it beside the pictures, on the drive they are on, so a second run of the wall
// costs a read rather than a decode.
//
// Nothing here spells out where the file lands: where a TV puts it is that TV's
// business, and the service names the file it made.
#include "graphics/SystemThumbnail.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/webos/Luna.h"

namespace {

// The picture the service made for the photo at path, no smaller than maxEdge
// on its long edge, or an empty string if it made none.
std::string thumbnailFile(const std::string &path, int maxEdge) {
    // SAM sets the app's id when it starts the app, and the service wants it.
    const char *appId = SDL_getenv("APPID");
    if (appId == nullptr) {
        return std::string();
    }
    // "fit" keeps the picture's shape and puts its long edge on the larger of
    // the two lengths, so asking for a square asks for that edge. "crop", which
    // the TV's gallery asks for, would fill a 4:3 tile and cut the sides off.
    const nlohmann::json request{
        {"appId", appId},
        {"sourceUri", path},
        {"mediaType", "image"},
        {"requestId", std::to_string(maxEdge) + "_" + path},
        {"targetUri", ""},
        {"targetWidth", maxEdge},
        {"targetHeight", maxEdge},
        {"scaleType", "fit"}};
    const std::string text =
        Luna::call("luna://com.webos.service.tnm/getThumbnail", request.dump());
    if (text.empty()) {
        return std::string();
    }
    const nlohmann::json reply = nlohmann::json::parse(text, nullptr, false);
    if (reply.is_discarded() || !reply.value("returnValue", false)) {
        return std::string();
    }
    const auto thumbnails = reply.find("thumbnails");
    if (thumbnails == reply.end() || !thumbnails->is_array() || thumbnails->empty()) {
        return std::string();
    }
    return thumbnails->front().get<std::string>();
}

}  // namespace

bool SystemThumbnail::supported() {
    return true;
}

Bitmap SystemThumbnail::load(const std::string &path, int maxEdge) {
    if (path.empty() || maxEdge <= 0) {
        return Bitmap();
    }
    const std::string file = thumbnailFile(path, maxEdge);
    if (file.empty()) {
        return Bitmap();
    }
    // A JPEG, whatever the name ends in. The service answers a request to crop
    // with .tn3 and one to fit with .tn4.
    std::vector<uint8_t> encoded;
    if (!Bitmap::readFile(file, &encoded)) {
        return Bitmap();
    }
    Bitmap upright = Bitmap::loadFromMemory(encoded.data(), encoded.size(), 0);
    // A photo smaller than the edge asked for comes back at its own size, which
    // the wall would have to enlarge. Decode the photo instead.
    if (!upright.valid() || std::max(upright.width(), upright.height()) < maxEdge) {
        return Bitmap();
    }
    upright.markOpaqueUnlessTransparent();
    const Bitmap reduced = upright.sampledFromWhole(
        Sampling::Picked, Bitmap::sampleSizeFor(upright.width(), upright.height(), maxEdge));
    // The service saves the picture upright, as the desktops' thumbnailers do.
    // The EXIF tag says how it is shown, and is read only once there is a
    // thumbnail to turn back.
    return reduced.toStoredOrientation(Bitmap::readExif(path).orientation);
}
