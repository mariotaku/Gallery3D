// Port of com.cooliris.media.MediaClustering. Groups time-ordered photos by temporal
// and geographic gaps; splits large clusters at pace changes and merges small ones.
// Time and size thresholds adapt to the album.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "MediaSet.h"

class MediaItem;

class MediaClustering {
  public:
    // Sizes the heuristics to the album. timeRangeMs is newest minus oldest.
    void setTimeRange(int64_t timeRangeMs, int numItems);

    void addItemForClustering(MediaItem *item);

    // Pass nullptr with processAllItems true to close out the last cluster.
    void compute(MediaItem *item, bool processAllItems);

    std::vector<std::unique_ptr<MediaSet>> &getClustersForDisplay() {
        return mClusters;
    }

    void clear();

    // Names each cluster after the span of dates it covers.
    void generateCaptions();

  private:
    void splitAndAddCurrentCluster();
    void mergeAndAddCurrentCluster();
    // Where the current cluster's pace changes most, or -1 if nowhere useful.
    int partitionIndexForCurrentCluster() const;
    static int64_t timeDistance(const MediaItem *a, const MediaItem *b);
    static bool isGeographicallySeparated(const MediaItem *a, const MediaItem *b);

    std::vector<std::unique_ptr<MediaSet>> mClusters;
    std::unique_ptr<MediaSet> mCurrentCluster;
    bool mCurrentSeparatedFromPrevious = false;

    int64_t mClusterSplitTime = (60000LL + 7200000LL) / 2;
    int64_t mLargeClusterSplitTime = mClusterSplitTime / 2;
    int mMinClusterSize = (8 + 15) / 2;
    int mMaxClusterSize = (20 + 50) / 2;
};
