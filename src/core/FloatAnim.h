// Port of com.cooliris.media.FloatAnim: eases toward a target over a fixed duration using frame
// time.
#pragma once

#include <cstdint>

// Port of com.cooliris.media.FloatAnim.
class FloatAnim {
  public:
    explicit FloatAnim(float value) : mValue(value) {}

    bool isAnimating() const {
        return mStartTime != 0;
    }

    float getTimeRemaining(uint64_t currentTime) const;
    float getValue(uint64_t currentTime);
    void animateValue(float value, float duration, uint64_t currentTime);
    void setValue(float value);
    void skip() {
        mStartTime = 0;
    }

  private:
    float getInterpolatedValue(uint64_t currentTime);

    float mValue = 0.0f;
    float mDelta = 0.0f;
    float mDuration = 0.0f;
    uint64_t mStartTime = 0;
};
