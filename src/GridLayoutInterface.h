// Port of com.cooliris.media.LayoutInterface and GridLayoutInterface.
#pragma once

#include "App.h"
#include "Vector3f.h"

class LayoutInterface {
  public:
    virtual ~LayoutInterface() = default;
    // Where the given slot sits, in pixels.
    virtual void getPositionForSlotIndex(int slotIndex, int itemWidth, int itemHeight, Vector3f &outPosition) = 0;
};

class GridLayoutInterface : public LayoutInterface {
  public:
    explicit GridLayoutInterface(int numRows) : mNumRows(numRows) {
        onDensityChanged();
    }

    // The gaps between slots are in pixels, so they follow the density. Called
    // again when the window moves to a display with a different scale.
    void onDensityChanged() {
        mSpacingX = (int)(20 * App::PIXEL_DENSITY);
        mSpacingY = (int)(40 * App::PIXEL_DENSITY);
    }

    float getSpacingForBreak() const {
        return (float)mSpacingX / 2.0f;
    }

    int getNextSlotIndexForBreak(int breakSlotIndex) const {
        int numRows = mNumRows;
        int mod = breakSlotIndex % numRows;
        int add = numRows - mod;
        if (add >= numRows) {
            add -= numRows;
        }
        return breakSlotIndex + add;
    }

    void getPositionForSlotIndex(int slotIndex, int itemWidth, int itemHeight, Vector3f &outPosition) override {
        int numRows = mNumRows;
        outPosition.x = (float)((slotIndex / numRows) * (itemWidth + mSpacingX));
        outPosition.y = (float)((slotIndex % numRows) * (itemHeight + mSpacingY));
        int maxY = (numRows - 1) * (itemHeight + mSpacingY);
        outPosition.y -= (float)(maxY >> 1);
        outPosition.z = 0.0f;
    }

    int mNumRows;
    int mSpacingX;
    int mSpacingY;
};
