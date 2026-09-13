// Port of com.cooliris.media.FloatUtils.
#pragma once

#include <cmath>

#include "core/Vector3f.h"

namespace FloatUtils {

const float ANIMATION_SPEED = 4.0f;

inline float animateAfterFactoringSpeed(float prevVal, float targetVal, float timeElapsed) {
    if (prevVal == targetVal) {
        return targetVal;
    }
    // Zero elapsed time must not trigger the convergence check and snap to the target.
    if (timeElapsed <= 0.0f) {
        return prevVal;
    }
    float newVal = prevVal + ((targetVal - prevVal) * timeElapsed);
    if (std::fabs(newVal - prevVal) < 0.0001f) {
        return targetVal;
    }
    if (newVal == prevVal) {
        return targetVal;
    }
    if (prevVal > targetVal && newVal < targetVal) {
        return targetVal;
    }
    if (prevVal < targetVal && newVal > targetVal) {
        return targetVal;
    }
    return newVal;
}

// Moves prevVal toward targetVal and clamps to it once it arrives.
inline float animate(float prevVal, float targetVal, float timeElapsed) {
    return animateAfterFactoringSpeed(prevVal, targetVal, timeElapsed * ANIMATION_SPEED);
}

inline float animateWithMaxSpeed(float prevVal, float targetVal, float timeElapsed, float maxSpeed) {
    float newTargetVal = targetVal;
    float delta = newTargetVal - prevVal;
    if (std::fabs(delta) > maxSpeed) {
        newTargetVal = prevVal + ((delta > 0.0f) ? maxSpeed : -maxSpeed);
    }
    return animateAfterFactoringSpeed(prevVal, newTargetVal, timeElapsed * ANIMATION_SPEED);
}

inline void animate(Vector3f &animVal, const Vector3f &targetVal, float timeElapsed) {
    timeElapsed = timeElapsed * ANIMATION_SPEED;
    animVal.x = animateAfterFactoringSpeed(animVal.x, targetVal.x, timeElapsed);
    animVal.y = animateAfterFactoringSpeed(animVal.y, targetVal.y, timeElapsed);
    animVal.z = animateAfterFactoringSpeed(animVal.z, targetVal.z, timeElapsed);
}

inline float clampMin(float val, float minVal) {
    return (val < minVal) ? minVal : val;
}

inline float clampMax(float val, float maxVal) {
    return (val > maxVal) ? maxVal : val;
}

inline float clamp(float val, float minVal, float maxVal) {
    if (val < minVal) {
        return minVal;
    }
    if (val > maxVal) {
        return maxVal;
    }
    return val;
}

inline int clamp(int val, int minVal, int maxVal) {
    if (val < minVal) {
        return minVal;
    }
    if (val > maxVal) {
        return maxVal;
    }
    return val;
}

inline bool boundsContainsPoint(float left, float right, float top, float bottom, float posX, float posY) {
    return !(posX < left || posX > right || posY < top || posY > bottom);
}

inline float maxOf(float a, float b) {
    return (a > b) ? a : b;
}

}  // namespace FloatUtils
