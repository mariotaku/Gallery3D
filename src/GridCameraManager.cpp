#include "GridCameraManager.h"

#include <cmath>

#include "FloatUtils.h"
#include "GridLayer.h"
#include "MediaFeed.h"
#include "Shared.h"

void GridCameraManager::centerCameraForSlot(LayoutInterface *layout, int slotIndex, float baseConvergence,
                                            const Vector3f &deltaAnchorPositionIn, int selectedSlotIndex,
                                            float zoomValue, float imageTheta, int state) {
    GridCamera *camera = mCamera;
    const bool zoomin = (selectedSlotIndex != Shared::INVALID);
    const int theta = (int)imageTheta;
    const int portrait = (theta / 90) % 2;
    if (slotIndex == selectedSlotIndex) {
        camera->mConvergenceSpeed = baseConvergence * 2.0f;
        camera->mFriction = 0.0f;
    }
    const float oneByZoom = 1.0f / zoomValue;
    if (slotIndex >= 0) {
        Vector3f position;
        Vector3f deltaAnchorPosition(deltaAnchorPositionIn);
        getSlotPositionForSlotIndex(slotIndex, camera, layout, deltaAnchorPosition, position);
        position.x = (zoomValue == 1.0f) ? (position.x * camera->mOneByScale) : camera->mLookAtX;
        position.y = (zoomValue == 1.0f) ? 0.0f : camera->mLookAtY;
        if (state == GridLayer::STATE_MEDIA_SETS || state == GridLayer::STATE_TIMELINE) {
            position.y = -0.1f;
        }
        float width = (float)camera->mItemWidth;
        float height = (float)camera->mItemHeight;
        if (portrait != 0) {
            std::swap(width, height);
        }
        camera->moveTo(position.x, position.y,
                       zoomin ? camera->getDistanceToFitRect(width * oneByZoom, height * oneByZoom) : 0.0f);
    } else {
        camera->moveYTo(0.0f);
        camera->moveZTo(0.0f);
    }
}

bool GridCameraManager::constrainCameraForSlot(LayoutInterface *layout, int slotIndex,
                                               const Vector3f &deltaAnchorPositionIn, float currentFocusItemWidth,
                                               float currentFocusItemHeight) {
    GridCamera *camera = mCamera;
    bool retVal = false;
    if (slotIndex < 0) {
        return false;
    }

    Vector3f position;
    Vector3f deltaAnchorPosition(deltaAnchorPositionIn);
    Vector3f topLeft;
    Vector3f bottomRight;
    Vector3f imgTopLeft;
    Vector3f imgBottomRight;

    getSlotPositionForSlotIndex(slotIndex, camera, layout, deltaAnchorPosition, position);
    position.x *= camera->mOneByScale;
    position.y = 0.0f;
    float width = currentFocusItemWidth / 2.0f;
    float height = currentFocusItemHeight / 2.0f;
    imgTopLeft.set(position.x - width, position.y - height, 0.0f);
    imgBottomRight.set(position.x + width, position.y + height, 0.0f);
    camera->convertToCameraSpace(0.0f, 0.0f, 0.0f, topLeft);
    camera->convertToCameraSpace((float)camera->mWidth, (float)camera->mHeight, 0.0f, bottomRight);
    camera->mConvergenceSpeed = 2.0f;
    camera->mFriction = 0.0f;

    // Gingerbread fix: when the viewport is wider than the image, centre it
    // instead of clamping each edge, otherwise a zoomed out photo drifts into
    // a corner. Only the smaller-viewport case still clamps.
    if ((bottomRight.x - topLeft.x) > (imgBottomRight.x - imgTopLeft.x)) {
        float hCenterExtent = (bottomRight.x + topLeft.x) / 2.0f - (imgBottomRight.x + imgTopLeft.x) / 2.0f;
        camera->moveBy(-hCenterExtent, 0.0f, 0.0f);
    } else {
        float leftExtent = topLeft.x - imgTopLeft.x;
        float rightExtent = bottomRight.x - imgBottomRight.x;
        if (leftExtent < 0.0f) {
            retVal = true;
            camera->moveBy(-leftExtent, 0.0f, 0.0f);
        }
        if (rightExtent > 0.0f) {
            retVal = true;
            camera->moveBy(-rightExtent, 0.0f, 0.0f);
        }
    }

    if ((bottomRight.y - topLeft.y) > (imgBottomRight.y - imgTopLeft.y)) {
        float vCenterExtent = (bottomRight.y + topLeft.y) / 2.0f - (imgBottomRight.y + imgTopLeft.y) / 2.0f;
        camera->moveBy(0.0f, -vCenterExtent, 0.0f);
    } else {
        float topExtent = topLeft.y - imgTopLeft.y;
        float bottomExtent = bottomRight.y - imgBottomRight.y;
        if (topExtent < 0.0f) {
            camera->moveBy(0.0f, -topExtent, 0.0f);
        }
        if (bottomExtent > 0.0f) {
            camera->moveBy(0.0f, -bottomExtent, 0.0f);
        }
    }
    return retVal;
}

void GridCameraManager::computeVisibleRange(MediaFeed *feed, LayoutInterface *layout,
                                            const Vector3f &deltaAnchorPositionIn, IndexRange &outVisibleRange,
                                            IndexRange &outBufferedVisibleRange, IndexRange &outCompleteRange,
                                            int state) {
    GridCamera *camera = mCamera;
    float offset = camera->mLookAtX * camera->mScale;
    int itemWidth = camera->mItemWidth;
    float maxIncrement = (float)camera->mWidth * 0.5f + (float)itemWidth;
    float left = -maxIncrement + offset;
    float right = left + 2.0f * maxIncrement;
    if (state == GridLayer::STATE_MEDIA_SETS || state == GridLayer::STATE_TIMELINE) {
        right += ((float)itemWidth * 0.5f);
    }
    float top = -maxIncrement;
    float bottom = (float)camera->mHeight + maxIncrement;

    int numSlots = (feed != nullptr) ? feed->getNumSlots() : 0;
    outCompleteRange.set(0, numSlots - 1);

    Vector3f position;
    Vector3f deltaAnchorPosition(deltaAnchorPositionIn);

    int firstVisibleSlotIndex = 0;
    int lastVisibleSlotIndex = numSlots - 1;
    int leftEdge = firstVisibleSlotIndex;
    int rightEdge = lastVisibleSlotIndex;
    int index = (leftEdge + rightEdge) / 2;
    lastVisibleSlotIndex = firstVisibleSlotIndex;

    // Binary search for any slot inside the view, then walk out both ways.
    while (index != leftEdge) {
        getSlotPositionForSlotIndex(index, camera, layout, deltaAnchorPosition, position);
        if (FloatUtils::boundsContainsPoint(left, right, top, bottom, position.x, position.y)) {
            firstVisibleSlotIndex = index;
            lastVisibleSlotIndex = index;
            break;
        }
        if (position.x > left) {
            rightEdge = index;
        } else {
            leftEdge = index;
        }
        index = (leftEdge + rightEdge) / 2;
    }
    while (firstVisibleSlotIndex >= 0 && firstVisibleSlotIndex < numSlots) {
        getSlotPositionForSlotIndex(firstVisibleSlotIndex, camera, layout, deltaAnchorPosition, position);
        if (!FloatUtils::boundsContainsPoint(left, right, top, bottom, position.x, position.y)) {
            ++firstVisibleSlotIndex;
            break;
        }
        --firstVisibleSlotIndex;
    }
    while (lastVisibleSlotIndex >= 0 && lastVisibleSlotIndex < numSlots) {
        getSlotPositionForSlotIndex(lastVisibleSlotIndex, camera, layout, deltaAnchorPosition, position);
        if (!FloatUtils::boundsContainsPoint(left, right, top, bottom, position.x, position.y)) {
            --lastVisibleSlotIndex;
            break;
        }
        ++lastVisibleSlotIndex;
    }
    if (firstVisibleSlotIndex < 0) {
        firstVisibleSlotIndex = 0;
    }
    if (lastVisibleSlotIndex >= numSlots) {
        lastVisibleSlotIndex = numSlots - 1;
    }
    outVisibleRange.set(firstVisibleSlotIndex, lastVisibleSlotIndex);
    if (feed != nullptr) {
        feed->setVisibleRange(firstVisibleSlotIndex, lastVisibleSlotIndex);
    }

    // Round out to a buffer of 24 slots so the display item array is stable
    // while scrolling.
    const int buffer = 24;
    firstVisibleSlotIndex = ((firstVisibleSlotIndex - buffer) / buffer) * buffer;
    lastVisibleSlotIndex += buffer;
    lastVisibleSlotIndex = (lastVisibleSlotIndex / buffer) * buffer;
    if (firstVisibleSlotIndex < 0) {
        firstVisibleSlotIndex = 0;
    }
    if (lastVisibleSlotIndex >= numSlots) {
        lastVisibleSlotIndex = numSlots - 1;
    }
    outBufferedVisibleRange.set(firstVisibleSlotIndex, lastVisibleSlotIndex);
}

void GridCameraManager::getSlotPositionForSlotIndex(int slotIndex, GridCamera *camera, LayoutInterface *layout,
                                                    const Vector3f &deltaAnchorPosition, Vector3f &outVal) {
    layout->getPositionForSlotIndex(slotIndex, camera->mItemWidth, camera->mItemHeight, outVal);
    outVal.subtract(deltaAnchorPosition);
}

float GridCameraManager::getFillScreenZoomValue(GridCamera *camera, float currentFocusItemWidth,
                                                float currentFocusItemHeight) {
    Vector3f topLeft;
    Vector3f bottomRight;
    camera->convertToCameraSpace(0.0f, 0.0f, 0.0f, topLeft);
    camera->convertToCameraSpace((float)camera->mWidth, (float)camera->mHeight, 0.0f, bottomRight);
    float xExtent = std::fabs(topLeft.x - bottomRight.x) / currentFocusItemWidth;
    float yExtent = std::fabs(topLeft.y - bottomRight.y) / currentFocusItemHeight;
    return std::max(xExtent, yExtent);
}
