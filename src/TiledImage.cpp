#include "TiledImage.h"

#include <algorithm>

#include "LocalDataSource.h"
#include "MediaItem.h"
#include "MediaSet.h"
#include "RenderView.h"

namespace {

int clampInt(int value, int low, int high) {
    return (value < low) ? low : ((value > high) ? high : value);
}

float clamp01(float value) {
    return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
}

}  // namespace

bool TiledImage::canTile(const MediaItem *item) {
    if (item == nullptr || !item->hasFullSize()) {
        return false;
    }
    MediaSet *set = item->mParentMediaSet;
    DataSource *source = (set != nullptr) ? set->mDataSource : nullptr;
    return source != nullptr && source->supportsRegions();
}

int64_t TiledImage::keyFor(int sampleSize, int column, int row) {
    // Pack sample size, column and row into twenty bits each.
    return ((int64_t)sampleSize << 40) | ((int64_t)column << 20) | (int64_t)row;
}

void TiledImage::clear() {
    mTiles.clear();
    mPlaced.clear();
    mSampleSize = 0;
    mLoadedCount = 0;
    mWantedCount = 0;
}

TiledImage::Grid TiledImage::gridFor(int fullWidth, int fullHeight, float drawnWidth) {
    Grid grid;
    if (fullWidth <= 0 || fullHeight <= 0 || drawnWidth <= 0.0f) {
        return grid;
    }
    // Choose the coarsest level with at least one source texel per screen pixel.
    while ((float)(fullWidth / (grid.sampleSize * 2)) >= drawnWidth && grid.sampleSize < (1 << 16)) {
        grid.sampleSize *= 2;
    }
    grid.regionEdge = kTileEdge * grid.sampleSize;
    grid.columns = (fullWidth + grid.regionEdge - 1) / grid.regionEdge;
    grid.rows = (fullHeight + grid.regionEdge - 1) / grid.regionEdge;
    return grid;
}

TiledImage::Region TiledImage::regionFor(const Grid &grid, int fullWidth, int fullHeight, int column, int row) {
    Region region;
    if (grid.regionEdge <= 0 || column < 0 || row < 0 || column >= grid.columns || row >= grid.rows) {
        return region;
    }
    region.x = column * grid.regionEdge;
    region.y = row * grid.regionEdge;
    // Trim edge tiles: the museum server returns 502 for rectangles outside the image.
    region.width = std::min(grid.regionEdge, fullWidth - region.x);
    region.height = std::min(grid.regionEdge, fullHeight - region.y);
    region.outWidth = std::max(1, region.width / grid.sampleSize);
    region.outHeight = std::max(1, region.height / grid.sampleSize);
    return region;
}

void TiledImage::update(RenderView *view, float left, float top, float right, float bottom, float drawnWidth) {
    mPlaced.clear();
    mLoadedCount = 0;
    mWantedCount = 0;
    if (view == nullptr || !canTile(mItem)) {
        return;
    }

    const int fullWidth = mItem->mFullWidth;
    const int fullHeight = mItem->mFullHeight;
    const Grid grid = gridFor(fullWidth, fullHeight, drawnWidth);
    if (grid.columns <= 0 || grid.rows <= 0) {
        return;
    }

    if (grid.sampleSize != mSampleSize) {
        // Drop tiles when the sample level changes; the new grid covers the same image at
        // another scale.
        mTiles.clear();
        mSampleSize = grid.sampleSize;
    }

    ++mPass;

    const int firstColumn = clampInt((int)(clamp01(left) * (float)fullWidth) / grid.regionEdge, 0, grid.columns - 1);
    const int lastColumn = clampInt((int)(clamp01(right) * (float)fullWidth) / grid.regionEdge, 0, grid.columns - 1);
    const int firstRow = clampInt((int)(clamp01(top) * (float)fullHeight) / grid.regionEdge, 0, grid.rows - 1);
    const int lastRow = clampInt((int)(clamp01(bottom) * (float)fullHeight) / grid.regionEdge, 0, grid.rows - 1);

    int started = 0;
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const int64_t key = keyFor(grid.sampleSize, column, row);
            auto found = mTiles.find(key);
            if (found == mTiles.end()) {
                if (started >= kMaxStartsPerUpdate) {
                    // Not this pass. It is still wanted, so the count below says
                    // the picture is not finished, and the next one picks it up.
                    ++mWantedCount;
                    continue;
                }
                const Region region = regionFor(grid, fullWidth, fullHeight, column, row);
                if (region.width <= 0 || region.height <= 0) {
                    continue;
                }
                Tile tile;
                tile.left = region.x;
                tile.top = region.y;
                tile.right = region.x + region.width;
                tile.bottom = region.y + region.height;
                tile.sampleSize = grid.sampleSize;
                tile.texture = std::make_shared<RegionTexture>(mItem, region.x, region.y, region.width, region.height,
                                                               region.outWidth, region.outHeight);
                found = mTiles.emplace(key, std::move(tile)).first;
                view->prime(found->second.texture, true);
                ++started;
            }

            Tile &tile = found->second;
            tile.lastWanted = mPass;
            ++mWantedCount;
            if (!tile.texture || !tile.texture->isLoaded()) {
                continue;
            }
            ++mLoadedCount;

            Placed placed;
            placed.texture = tile.texture;
            placed.left = (float)tile.left / (float)fullWidth;
            placed.top = (float)tile.top / (float)fullHeight;
            placed.right = (float)tile.right / (float)fullWidth;
            placed.bottom = (float)tile.bottom / (float)fullHeight;
            mPlaced.push_back(placed);
        }
    }

    if (mTiles.size() <= (size_t)kMaxTiles) {
        return;
    }
    // Over budget. The ones wanted longest ago go first, which leaves whatever
    // the last few frames have been looking at.
    std::vector<std::pair<uint64_t, int64_t>> byAge;
    byAge.reserve(mTiles.size());
    for (const auto &entry : mTiles) {
        byAge.emplace_back(entry.second.lastWanted, entry.first);
    }
    std::sort(byAge.begin(), byAge.end());
    const size_t toDrop = mTiles.size() - (size_t)kMaxTiles;
    for (size_t i = 0; i < toDrop; ++i) {
        if (byAge[i].first == mPass) {
            // Everything left is on screen now. Dropping one of those would
            // only make it be fetched again next frame.
            break;
        }
        mTiles.erase(byAge[i].second);
    }
}
