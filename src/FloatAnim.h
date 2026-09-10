// Port of com.cooliris.media.FloatAnim: a value that eases towards a target
// over a fixed duration, driven by the frame clock rather than stepped once per
// frame. It lived in GridDrawManager while the draw code was its only user.
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
