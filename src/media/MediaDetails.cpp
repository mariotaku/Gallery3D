#include "media/MediaDetails.h"

#include <algorithm>

#include "core/DateLabels.h"
#include "media/MediaBucketList.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"

namespace MediaDetails {

namespace {

// A whole album is selected rather than items inside it.
bool isSetSelection(const MediaBucket &bucket) {
    return bucket.mediaSet != nullptr && !bucket.hasItems;
}

bool isSetSelection(const std::vector<MediaBucket> &buckets) {
    if (buckets.empty()) {
        return false;
    }
    // More than one bucket can only have come from picking albums.
    return buckets.size() > 1 || isSetSelection(buckets.front());
}

// Multiple-item selections use the first bucket, as built by addSlotToSelectedItems.
bool isMultipleItemSelection(const std::vector<MediaBucket> &buckets) {
    return !buckets.empty() && buckets.front().mediaItems.size() > 1;
}

const MediaItem *firstItem(const std::vector<MediaBucket> &buckets) {
    for (const MediaBucket &bucket : buckets) {
        if (!isSetSelection(bucket) && !bucket.mediaItems.empty()) {
            return bucket.mediaItems.front();
        }
    }
    return nullptr;
}

const MediaSet *firstSet(const std::vector<MediaBucket> &buckets) {
    for (const MediaBucket &bucket : buckets) {
        if (isSetSelection(bucket)) {
            return bucket.mediaSet;
        }
    }
    return nullptr;
}

// Uppercase file extension for display.
std::string displayType(const std::string &mimeType) {
    const size_t slash = mimeType.find('/');
    std::string type = (slash != std::string::npos && slash + 1 < mimeType.size()) ? mimeType.substr(slash + 1)
                                                                                  : mimeType;
    for (char &c : type) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
    }
    return type;
}

std::string countLine(int count, const char *singular, const char *plural) {
    return std::to_string(count) + " " + ((count == 1) ? singular : plural);
}

// Collect date and location bounds for the selection.
struct Span {
    int items = 0;
    int64_t earliest = 0;
    int64_t latest = 0;
    bool dated = false;
    // The coarsest any one item was known to, since a span is only as precise
    // as its vaguest member.
    int precision = MediaItem::PRECISION_DAY;
    std::string location;
};

void gather(const MediaItem *item, Span *span) {
    if (item == nullptr) {
        return;
    }
    ++span->items;
    if (!item->isDateTakenValid()) {
        return;
    }
    if (!span->dated) {
        span->dated = true;
        span->earliest = item->mDateTakenInMs;
        span->latest = item->mDateTakenInMs;
    } else {
        span->earliest = std::min(span->earliest, item->mDateTakenInMs);
        span->latest = std::max(span->latest, item->mDateTakenInMs);
    }
    span->precision = std::max(span->precision, item->mDatePrecision);
}

std::vector<std::string> linesForSpan(const Span &span, int albums) {
    std::vector<std::string> lines;
    lines.push_back(countLine(albums, "album selected", "albums selected"));
    lines.push_back(countLine(span.items, "item selected", "items selected"));
    if (span.dated) {
        lines.push_back("Start: " + DateLabels::atPrecision(span.earliest, span.precision));
        lines.push_back("End: " + DateLabels::atPrecision(span.latest, span.precision));
    } else {
        lines.push_back("Start: Date unknown");
        lines.push_back("End: Date unknown");
    }
    if (!span.location.empty()) {
        lines.push_back("Location: " + span.location);
    }
    return lines;
}

std::vector<std::string> linesForItem(const MediaItem *item) {
    std::vector<std::string> lines;
    lines.push_back("Title: " + item->mCaption);
    lines.push_back("Type: " + displayType(item->mMimeType));
    if (item->isDateTakenValid()) {
        lines.push_back("Taken on: " + DateLabels::atPrecision(item->mDateTakenInMs, item->mDatePrecision));
    } else if (item->isDateAddedValid()) {
        lines.push_back("Taken on: " + DateLabels::dayMonthYear(item->mDateAddedInSec * 1000));
    } else {
        lines.push_back("Taken on: Date unknown");
    }
    const MediaSet *parent = item->mParentMediaSet;
    lines.push_back(parent != nullptr ? "Album: " + parent->mName : std::string("Album:"));
    // Without reverse geocoding, coordinates provide no place name.
    lines.push_back("Location: Unknown location");
    return lines;
}

}  // namespace

std::vector<std::string> linesFor(const MediaBucketList &selection) {
    const std::vector<MediaBucket> &buckets = selection.get();
    if (buckets.empty()) {
        return std::vector<std::string>();
    }

    if (isSetSelection(buckets) && buckets.size() == 1) {
        // One whole album, which already knows its own span.
        const MediaSet *set = firstSet(buckets);
        if (set == nullptr) {
            return std::vector<std::string>();
        }
        Span span;
        span.items = set->getNumItems();
        span.dated = set->areTimestampsAvailable();
        span.earliest = set->mMinTimestamp;
        span.latest = set->mMaxTimestamp;
        span.precision = set->datePrecision();
        if (set->mLatLongDetermined) {
            span.location = set->mReverseGeocodedLocation;
        }
        return linesForSpan(span, 1);
    }

    if (isSetSelection(buckets) || isMultipleItemSelection(buckets)) {
        Span span;
        for (const MediaBucket &bucket : buckets) {
            if (isSetSelection(bucket)) {
                if (bucket.mediaSet != nullptr) {
                    for (const MediaItem *item : bucket.mediaSet->getItems()) {
                        gather(item, &span);
                    }
                    if (span.location.empty() && bucket.mediaSet->mLatLongDetermined) {
                        span.location = bucket.mediaSet->mReverseGeocodedLocation;
                    }
                }
            } else {
                for (const MediaItem *item : bucket.mediaItems) {
                    gather(item, &span);
                }
            }
        }
        return linesForSpan(span, (int)buckets.size());
    }

    const MediaItem *item = firstItem(buckets);
    if (item == nullptr) {
        return std::vector<std::string>();
    }
    return linesForItem(item);
}

}  // namespace MediaDetails
