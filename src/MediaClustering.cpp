#include "MediaClustering.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "MediaItem.h"
#include "Shared.h"

namespace {

// All of these are the original's numbers.
const double GEOGRAPHIC_DISTANCE_CUTOFF_IN_MILES = 20.0;
const int64_t MIN_CLUSTER_SPLIT_TIME_IN_MS = 60000LL;
const int64_t MAX_CLUSTER_SPLIT_TIME_IN_MS = 7200000LL;
const int NUM_CLUSTERS_TARGETED = 9;
const int MIN_MIN_CLUSTER_SIZE = 8;
const int MAX_MIN_CLUSTER_SIZE = 15;
const int MIN_MAX_CLUSTER_SIZE = 20;
const int MAX_MAX_CLUSTER_SIZE = 50;
const int CLUSTER_SPLIT_MULTIPLIER = 3;
const float MIN_PARTITION_CHANGE_FACTOR = 2.0f;
const int PARTITION_CLUSTER_SPLIT_TIME_FACTOR = 2;

MediaItem *lastItemOf(const MediaSet &set) {
    const std::vector<MediaItem *> &items = set.getItems();
    return items.empty() ? nullptr : items.back();
}

// Formats one timestamp. Local time, because that is how a camera wrote it.
std::string formatTime(int64_t millis, const char *format) {
    std::time_t seconds = (std::time_t)(millis / 1000);
    std::tm parts {};
#if defined(_WIN32)
    localtime_s(&parts, &seconds);
#else
    localtime_r(&seconds, &parts);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), format, &parts) == 0) {
        return std::string();
    }
    return std::string(buffer);
}

}  // namespace

void MediaClustering::clear() {
    mClusters.clear();
    mCurrentCluster.reset();
    mCurrentSeparatedFromPrevious = false;
}

void MediaClustering::setTimeRange(int64_t timeRangeMs, int numItems) {
    if (numItems != 0) {
        // Aim for a readable number of piles, then let the album's own pace
        // decide how long a gap has to be before it starts a new one.
        int meanItemsPerCluster = numItems / NUM_CLUSTERS_TARGETED;
        mMinClusterSize = meanItemsPerCluster / 2;
        mMaxClusterSize = meanItemsPerCluster * 2;
        mClusterSplitTime = (timeRangeMs / numItems) * CLUSTER_SPLIT_MULTIPLIER;
    }
    mClusterSplitTime =
        std::min(std::max(mClusterSplitTime, MIN_CLUSTER_SPLIT_TIME_IN_MS), MAX_CLUSTER_SPLIT_TIME_IN_MS);
    mLargeClusterSplitTime = mClusterSplitTime / PARTITION_CLUSTER_SPLIT_TIME_FACTOR;
    mMinClusterSize = Shared::clamp(mMinClusterSize, MIN_MIN_CLUSTER_SIZE, MAX_MIN_CLUSTER_SIZE);
    mMaxClusterSize = Shared::clamp(mMaxClusterSize, MIN_MAX_CLUSTER_SIZE, MAX_MAX_CLUSTER_SIZE);
}

int64_t MediaClustering::timeDistance(const MediaItem *a, const MediaItem *b) {
    if (a == nullptr || b == nullptr) {
        return 0;
    }
    int64_t delta = a->mDateTakenInMs - b->mDateTakenInMs;
    return delta < 0 ? -delta : delta;
}

bool MediaClustering::isGeographicallySeparated(const MediaItem *a, const MediaItem *b) {
    if (a == nullptr || b == nullptr || !a->isLatLongValid() || !b->isLatLongValid()) {
        return false;
    }
    // Equirectangular approximation. Good enough at the 20 mile scale this is
    // asked about, and far cheaper than a great circle.
    const double kPi = 3.14159265358979323846;
    const double milesPerDegree = 69.0;
    double meanLatitude = (a->mLatitude + b->mLatitude) * 0.5 * kPi / 180.0;
    double dLat = (a->mLatitude - b->mLatitude) * milesPerDegree;
    double dLon = (a->mLongitude - b->mLongitude) * milesPerDegree * std::cos(meanLatitude);
    return std::sqrt(dLat * dLat + dLon * dLon) > GEOGRAPHIC_DISTANCE_CUTOFF_IN_MILES;
}

void MediaClustering::addItemForClustering(MediaItem *item) {
    compute(item, false);
}

void MediaClustering::compute(MediaItem *currentItem, bool processAllItems) {
    if (!mCurrentCluster) {
        mCurrentCluster = std::make_unique<MediaSet>();
        mCurrentCluster->mType = MediaSet::TYPE_SMART;
    }

    if (currentItem != nullptr) {
        int numClusters = (int)mClusters.size();
        int numCurrentItems = mCurrentCluster->getNumItems();
        bool geographicallySeparate = false;
        bool addedToCurrent = false;

        if (numCurrentItems == 0) {
            mCurrentCluster->addItemRef(currentItem);
            addedToCurrent = true;
        } else {
            MediaItem *previous = lastItemOf(*mCurrentCluster);
            if (isGeographicallySeparated(previous, currentItem)) {
                mClusters.push_back(std::move(mCurrentCluster));
                geographicallySeparate = true;
            } else if (numCurrentItems > mMaxClusterSize) {
                splitAndAddCurrentCluster();
            } else if (timeDistance(previous, currentItem) < mClusterSplitTime) {
                mCurrentCluster->addItemRef(currentItem);
                addedToCurrent = true;
            } else if (numClusters > 0 && numCurrentItems < mMinClusterSize && !mCurrentSeparatedFromPrevious) {
                mergeAndAddCurrentCluster();
            } else {
                mClusters.push_back(std::move(mCurrentCluster));
            }

            if (!addedToCurrent) {
                mCurrentCluster = std::make_unique<MediaSet>();
                mCurrentCluster->mType = MediaSet::TYPE_SMART;
                mCurrentSeparatedFromPrevious = geographicallySeparate;
                mCurrentCluster->addItemRef(currentItem);
            }
        }
    }

    if (processAllItems && mCurrentCluster && mCurrentCluster->getNumItems() > 0) {
        int numClusters = (int)mClusters.size();
        int numCurrentItems = mCurrentCluster->getNumItems();
        // The last cluster is the one most likely to be the wrong size.
        if (numCurrentItems > mMaxClusterSize) {
            splitAndAddCurrentCluster();
        } else if (numClusters > 0 && numCurrentItems < mMinClusterSize && !mCurrentSeparatedFromPrevious) {
            mergeAndAddCurrentCluster();
        } else {
            mClusters.push_back(std::move(mCurrentCluster));
        }
        mCurrentCluster.reset();
    }
}

int MediaClustering::partitionIndexForCurrentCluster() const {
    int partitionIndex = -1;
    float largestChange = MIN_PARTITION_CHANGE_FACTOR;
    const std::vector<MediaItem *> &items = mCurrentCluster->getItems();
    int numItems = (int)items.size();

    if (numItems > mMinClusterSize + 1) {
        // Look for the biggest change in pace, not simply the biggest gap: a
        // steady stream of shots should not be cut just because it is long.
        for (int i = mMinClusterSize; i < numItems - mMinClusterSize; ++i) {
            MediaItem *previous = items[(size_t)i - 1];
            MediaItem *current = items[(size_t)i];
            MediaItem *next = items[(size_t)i + 1];
            if (!previous->isDateTakenValid() || !current->isDateTakenValid() || !next->isDateTakenValid()) {
                continue;
            }
            float diff1 = (float)timeDistance(next, current);
            float diff2 = (float)timeDistance(current, previous);
            float change = std::max(diff1 / (diff2 + 0.01f), diff2 / (diff1 + 0.01f));
            if (change > largestChange) {
                if (timeDistance(current, previous) > mLargeClusterSplitTime) {
                    partitionIndex = i;
                    largestChange = change;
                } else if (timeDistance(next, current) > mLargeClusterSplitTime) {
                    partitionIndex = i + 1;
                    largestChange = change;
                }
            }
        }
    }
    return partitionIndex;
}

void MediaClustering::splitAndAddCurrentCluster() {
    const std::vector<MediaItem *> items = mCurrentCluster->getItems();
    int numItems = (int)items.size();
    int secondPartitionStart = partitionIndexForCurrentCluster();
    if (secondPartitionStart == -1) {
        mClusters.push_back(std::move(mCurrentCluster));
        return;
    }

    auto first = std::make_unique<MediaSet>();
    first->mType = MediaSet::TYPE_SMART;
    for (int i = 0; i < secondPartitionStart; ++i) {
        first->addItemRef(items[(size_t)i]);
    }
    mClusters.push_back(std::move(first));

    auto second = std::make_unique<MediaSet>();
    second->mType = MediaSet::TYPE_SMART;
    for (int i = secondPartitionStart; i < numItems; ++i) {
        second->addItemRef(items[(size_t)i]);
    }
    mClusters.push_back(std::move(second));
    mCurrentCluster.reset();
}

void MediaClustering::mergeAndAddCurrentCluster() {
    MediaSet *previous = mClusters.back().get();
    // Only fold into the previous cluster if it is also short. Merging a runt
    // into an already full cluster would just make an oversized one.
    if (previous->getNumItems() < mMinClusterSize) {
        for (MediaItem *item : mCurrentCluster->getItems()) {
            previous->addItemRef(item);
        }
        mCurrentCluster.reset();
    } else {
        mClusters.push_back(std::move(mCurrentCluster));
    }
}

void MediaClustering::generateCaptions() {
    for (std::unique_ptr<MediaSet> &cluster : mClusters) {
        int64_t minTimestamp = -1;
        int64_t maxTimestamp = -1;
        if (cluster->areTimestampsAvailable()) {
            minTimestamp = cluster->mMinTimestamp;
            maxTimestamp = cluster->mMaxTimestamp;
        } else if (cluster->mMinAddedTimestamp <= cluster->mMaxAddedTimestamp && cluster->mMaxAddedTimestamp > 0) {
            minTimestamp = cluster->mMinAddedTimestamp;
            maxTimestamp = cluster->mMaxAddedTimestamp;
        }

        if (minTimestamp < 0) {
            cluster->mName.clear();
        } else {
            std::string minDay = formatTime(minTimestamp, "%Y%m%d");
            std::string maxDay = formatTime(maxTimestamp, "%Y%m%d");
            std::string minYear = formatTime(minTimestamp, "%Y");
            std::string maxYear = formatTime(maxTimestamp, "%Y");
            if (minDay == maxDay) {
                cluster->mName = formatTime(minTimestamp, "%d %b %Y");
            } else if (minYear == maxYear) {
                // Same year, so the year only needs saying once.
                cluster->mName = formatTime(minTimestamp, "%d %b") + " - " + formatTime(maxTimestamp, "%d %b %Y");
            } else {
                cluster->mName = formatTime(minTimestamp, "%b %Y") + " - " + formatTime(maxTimestamp, "%b %Y");
            }
        }
        cluster->updateNumExpectedItems();
        // Not truncated: generateTitle's ellipsis is meant for long folder
        // names and turns "Oct 2020 - Feb 2021" into "Oct 2020 - F...2021".
        // The caption is short by construction, and the label texture shrinks
        // the font to fit anyway.
        cluster->generateTitle(false);
    }
}
