#include "FloatAnim.h"

#include <cmath>

float FloatAnim::getTimeRemaining(uint64_t currentTime) const {
    float duration = (float)(currentTime - mStartTime) * 0.001f;
    return (mDuration > duration) ? (mDuration - duration) : 0.0f;
}

float FloatAnim::getValue(uint64_t currentTime) {
    if (mStartTime == 0) {
        return mValue;
    }
    return getInterpolatedValue(currentTime);
}

void FloatAnim::animateValue(float value, float duration, uint64_t currentTime) {
    mDelta = getValue(currentTime) - value;
    mValue = value;
    mDuration = duration;
    mStartTime = currentTime;
}

void FloatAnim::setValue(float value) {
    mValue = value;
    mStartTime = 0;
}

float FloatAnim::getInterpolatedValue(uint64_t currentTime) {
    float ratio = (float)(currentTime - mStartTime) * 0.001f / mDuration;
    if (ratio >= 1.0f) {
        mStartTime = 0;
        return mValue;
    }
    ratio = 0.5f - 0.5f * std::cos(ratio * 3.14159265f);
    return mValue + (1.0f - ratio) * mDelta;
}
