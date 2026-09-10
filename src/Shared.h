// Port of com.cooliris.media.Shared.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace Shared {

const int INVALID = -1;
const int INFINITY_ = std::numeric_limits<int>::max();

inline bool isPowerOf2(int n) {
    return (n & -n) == n;
}

// Returns 0, +1, -1, +2, -2, +3, -3 ...
inline int midPointIterator(int i) {
    if (i != 0) {
        int tick = ((i - 1) / 2) + 1;
        int pass = ((i - 1) % 2 == 0) ? 1 : -1;
        return tick * pass;
    }
    return 0;
}

inline int nextPowerOf2(int n) {
    unsigned int v = (unsigned int)(n - 1);
    v |= v >> 16;
    v |= v >> 8;
    v |= v >> 4;
    v |= v >> 2;
    v |= v >> 1;
    return (int)(v + 1);
}

inline int clamp(int value, int minValue, int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

inline float scaleToFit(float srcWidth, float srcHeight, float outerWidth, float outerHeight, bool clipToFit) {
    float scaleX = outerWidth / srcWidth;
    float scaleY = outerHeight / srcHeight;
    return (clipToFit ? scaleX > scaleY : scaleX < scaleY) ? scaleX : scaleY;
}

// Returns an angle between 0 and 360 degrees for any input angle.
inline float normalizePositive(float angleToRotate) {
    if (angleToRotate == 0.0f) {
        return 0.0f;
    }
    float nf = angleToRotate / 360.0f;
    int n = 0;
    if (angleToRotate < 0) {
        n = (int)(nf - 1.0f);
    } else if (angleToRotate > 360) {
        n = (int)nf;
    }
    angleToRotate -= (n * 360.0f);
    if (angleToRotate == 360.0f) {
        angleToRotate = 0;
    }
    return angleToRotate;
}

inline float exifOrientationToDegrees(int exifOrientation) {
    switch (exifOrientation) {
    case 6:
        return 90.0f;
    case 3:
        return 180.0f;
    case 8:
        return 270.0f;
    default:
        return 0.0f;
    }
}

}  // namespace Shared
