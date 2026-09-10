// Port of com.cooliris.media.GridCameraManager.
#pragma once

#include "GridCamera.h"
#include "GridLayoutInterface.h"
#include "IndexRange.h"
#include "Vector3f.h"

class MediaFeed;

class GridCameraManager {
  public:
    explicit GridCameraManager(GridCamera *camera) : mCamera(camera) {}

    void centerCameraForSlot(LayoutInterface *layout, int slotIndex, float baseConvergence,
                             const Vector3f &deltaAnchorPositionIn, int selectedSlotIndex, float zoomValue,
                             float imageTheta, int state);

    // Returns true when the camera had to be pulled back inside the image.
    bool constrainCameraForSlot(LayoutInterface *layout, int slotIndex, const Vector3f &deltaAnchorPositionIn,
                                float currentFocusItemWidth, float currentFocusItemHeight);

    void computeVisibleRange(MediaFeed *feed, LayoutInterface *layout, const Vector3f &deltaAnchorPositionIn,
                             IndexRange &outVisibleRange, IndexRange &outBufferedVisibleRange,
                             IndexRange &outCompleteRange, int state);

    static void getSlotPositionForSlotIndex(int slotIndex, GridCamera *camera, LayoutInterface *layout,
                                            const Vector3f &deltaAnchorPosition, Vector3f &outVal);

    static float getFillScreenZoomValue(GridCamera *camera, float currentFocusItemWidth, float currentFocusItemHeight);

  private:
    GridCamera *mCamera;
};
