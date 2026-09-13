// Port of com.cooliris.media.DisplayList.
// Owns one DisplayItem per MediaItem and updates only animating items.
#pragma once

#include <map>
#include <memory>
#include <vector>

#include "grid/DisplayItem.h"

class MediaItem;

class DisplayList {
  public:
    DisplayItem *get(MediaItem *item);

    void setPositionAndStackIndex(DisplayItem *item, const Vector3f &position, int stackId, bool performTransition);
    void setHasFocus(DisplayItem *item, bool hasFocus, bool pushDown);
    // Every frame for every item without focus. Hovering is reapplied each
    // time, since a stack's offsets are reset every frame.
    void setHovered(DisplayItem *item, bool hovered, bool pushDown);
    void setOffset(DisplayItem *item, bool useOffset, bool pushDown, float span, float dx1, float dy1, float dx2,
                   float dy2);
    void setSingleOffset(DisplayItem *item, bool useOffset, bool pushAway, float x, float y, float z,
                         float spreadValue);

    void update(float timeElapsed);

    int getNumAnimatables() const {
        return (int)mAnimatables.size();
    }

    void setAlive(DisplayItem *item, bool alive);
    void commit(DisplayItem *item);
    void addToAnimatables(DisplayItem *item);

    void clear();
    void clearExcept(DisplayItem *const *displayItems, size_t count);

  private:
    void markIfDirty(DisplayItem *item);

    std::map<MediaItem *, std::unique_ptr<DisplayItem>> mDisplayMap;
    std::vector<DisplayItem *> mAnimatables;
};
