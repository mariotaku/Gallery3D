#include "Storage.h"

#include <nlohmann/json.hpp>

#include <SDL3/SDL.h>

#include "Luna.h"

namespace Storage {

std::vector<std::string> mediaRoots() {
    // The media database lists the pictures themselves, and the TV's own
    // gallery reads it, but its records belong to the indexer: db8 grants them
    // to com.lge, com.palm and com.webos callers and refuses everyone else with
    // "db: permission denied". The storage manager carries no such grant and
    // answers any caller.
    const std::string text =
        Luna::call("luna://com.webos.service.attachedstoragemanager/listDevices", "{}");
    if (text.empty()) {
        return std::vector<std::string>();
    }
    const nlohmann::json reply = nlohmann::json::parse(text, nullptr, false);
    if (reply.is_discarded() || !reply.value("returnValue", false)) {
        SDL_Log("storage: the TV refused to list its volumes: %s", text.c_str());
        return std::vector<std::string>();
    }
    const auto devices = reply.find("devices");
    if (devices == reply.end() || !devices->is_array()) {
        return std::vector<std::string>();
    }
    std::vector<std::string> roots;
    for (const nlohmann::json &device : *devices) {
        // A drive reports each of its partitions as a subdevice with a mount
        // point of its own, and the drive's own point is where those are hung
        // rather than where the files are. Take the partitions where there are
        // any, so a drive that mounts nothing itself is still read.
        const auto subDevices = device.find("subDevices");
        size_t added = 0;
        if (subDevices != device.end() && subDevices->is_array()) {
            for (const nlohmann::json &subDevice : *subDevices) {
                const std::string uri = subDevice.value("deviceUri", std::string());
                if (!uri.empty()) {
                    roots.push_back(uri);
                    ++added;
                }
            }
        }
        if (added > 0) {
            continue;
        }
        const std::string uri = device.value("deviceUri", std::string());
        if (!uri.empty()) {
            roots.push_back(uri);
        }
    }
    return roots;
}

}  // namespace Storage
