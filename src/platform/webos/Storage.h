// Where a TV keeps media. The storage manager names every volume the TV has
// attached: a USB drive, the internal sample photos, whatever the firmware
// offers. It is the TV's answer to the Pictures library, and unlike the media
// database it is a plain service an app may call.
#pragma once

#include <string>
#include <vector>

namespace Storage {

// Every attached volume's mount point, in the order the TV lists them. Empty
// when the storage manager cannot be reached.
std::vector<std::string> mediaRoots();

}  // namespace Storage
