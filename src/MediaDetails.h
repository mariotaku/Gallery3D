// Port of com.cooliris.media.DetailMode: read-only selection details.
// Single items show metadata; multiple items or albums show counts and shared spans.
#pragma once

#include <string>
#include <vector>

class MediaBucketList;

namespace MediaDetails {

std::vector<std::string> linesFor(const MediaBucketList &selection);

}  // namespace MediaDetails
