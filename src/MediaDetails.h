// Port of com.cooliris.media.DetailMode: what is known about what is selected,
// as lines of text.
//
// Read only throughout. Unlike delete and rotate it asks nothing of the storage
// behind the wall, so it is offered whatever the source allows.
//
// Two shapes, as in the original. One item gives its title, type, date, album
// and location. Anything else - several items, or whole albums - gives how many
// albums, how many items, the first and last date, and the location they share.
#pragma once

#include <string>
#include <vector>

class MediaBucketList;

namespace MediaDetails {

std::vector<std::string> linesFor(const MediaBucketList &selection);

}  // namespace MediaDetails
