#include "DisplayList.h"

#include <algorithm>

#include "MediaItem.h"

DisplayItem *DisplayList::get(MediaItem *item) {
    auto it = mDisplayMap.find(item);
    if (it != mDisplayMap.end()) {
        return it->second.get();
    }
    auto displayItem = std::make_unique<DisplayItem>(item);
    DisplayItem *raw = displayItem.get();
    mDisplayMap[item] = std::move(displayItem);
    return raw;
}

void DisplayList::setPositionAndStackIndex(DisplayItem *item, const Vector3f &position, int stackId,
                                           bool performTransition) {
    item->set(position, stackId, performTransition);
    if (!performTransition) {
        item->commit();
    } else {
        markIfDirty(item);
    }
}

void DisplayList::setHasFocus(DisplayItem *item, bool hasFocus, bool pushDown) {
    if (item->getHasFocus() != hasFocus) {
        item->setHasFocus(hasFocus, pushDown);
        markIfDirty(item);
    }
}

void DisplayList::setOffset(DisplayItem *item, bool useOffset, bool pushDown, float span, float dx1, float dy1,
                            float dx2, float dy2) {
    item->setOffset(useOffset, pushDown, span, dx1, dy1, dx2, dy2);
    markIfDirty(item);
}

void DisplayList::setSingleOffset(DisplayItem *item, bool useOffset, bool pushAway, float x, float y, float z,
                                  float spreadValue) {
    item->setSingleOffset(useOffset, pushAway, x, y, z, spreadValue);
    markIfDirty(item);
}

void DisplayList::update(float timeElapsed) {
    size_t writeIndex = 0;
    for (size_t i = 0; i < mAnimatables.size(); ++i) {
        DisplayItem *item = mAnimatables[i];
        item->update(timeElapsed);
        if (item->isAnimating()) {
            mAnimatables[writeIndex++] = item;
        } else {
            item->mInAnimatables = false;
        }
    }
    mAnimatables.resize(writeIndex);
}

void DisplayList::setAlive(DisplayItem *item, bool alive) {
    item->mAlive = alive;
    if (alive && item->isAnimating() && !item->mInAnimatables) {
        item->mInAnimatables = true;
        mAnimatables.push_back(item);
    }
}

void DisplayList::commit(DisplayItem *item) {
    item->commit();
    if (item->mInAnimatables) {
        item->mInAnimatables = false;
        mAnimatables.erase(std::remove(mAnimatables.begin(), mAnimatables.end(), item), mAnimatables.end());
    }
}

void DisplayList::addToAnimatables(DisplayItem *item) {
    if (!item->mInAnimatables) {
        item->mInAnimatables = true;
        mAnimatables.push_back(item);
    }
}

void DisplayList::markIfDirty(DisplayItem *item) {
    if (item->isAnimating()) {
        addToAnimatables(item);
    }
}

void DisplayList::clear() {
    mAnimatables.clear();
    mDisplayMap.clear();
}

void DisplayList::clearExcept(DisplayItem *const *displayItems, size_t count) {
    // Keeps only the items currently on screen, dropping the thumbnails of
    // everything else.
    std::map<MediaItem *, std::unique_ptr<DisplayItem>> kept;
    for (size_t i = 0; i < count; ++i) {
        DisplayItem *displayItem = displayItems[i];
        if (!displayItem) {
            continue;
        }
        auto it = mDisplayMap.find(displayItem->mItemRef);
        if (it != mDisplayMap.end() && it->second) {
            kept[displayItem->mItemRef] = std::move(it->second);
        }
    }
    mAnimatables.clear();
    for (auto &entry : kept) {
        entry.second->mInAnimatables = false;
    }
    mDisplayMap = std::move(kept);
}
