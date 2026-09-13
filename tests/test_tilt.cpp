// Leaning the wall from the accelerometer.
//
// This is the one part of the port that was deleted rather than translated -
// a desktop has no tilt - so it went back in from the original source rather
// than from memory. The constants here are that source's, including one that
// does not do what it looks like it does.
#include "tests.h"

#include "grid/GridCamera.h"
#include "grid/GridInputProcessor.h"
#include "grid/GridLayer.h"

namespace {

struct Rig {
    Rig() : camera(0, 0, 96, 72), processor(&camera, nullptr, nullptr, nullptr) {}
    GridCamera camera;
    GridInputProcessor processor;
};

// What the wall's eye offset becomes for a given reading along the screen.
float tiltFor(float alongScreen, int state = GridLayer::STATE_MEDIA_SETS) {
    Rig rig;
    rig.processor.onSensorChanged(nullptr, alongScreen, 0.0f, 0.0f, state);
    return rig.camera.mEyeOffsetX;
}

}  // namespace

TEST(a_level_device_does_not_lean_the_wall) {
    CHECK_NEAR(tiltFor(0.0f), 0.0f, 0.0001f);
}

TEST(a_small_tilt_is_inside_the_deadzone) {
    // The original drops anything under half a unit after filtering, so a hand
    // that is not quite steady does not set the wall drifting.
    //
    // The filter is a fifth of the reading (see below), so the deadzone bites
    // until the reading itself passes 2.5.
    CHECK_NEAR(tiltFor(2.0f), 0.0f, 0.0001f);
    CHECK_NEAR(tiltFor(-2.0f), 0.0f, 0.0001f);
}

TEST(a_real_tilt_leans_the_wall_the_other_way) {
    // 0.2 of the reading, then three units of eye offset per unit of that, and
    // negated: the wall leans away from the raised edge.
    CHECK_NEAR(tiltFor(5.0f), -3.0f, 0.0001f);
    CHECK_NEAR(tiltFor(-5.0f), 3.0f, 0.0001f);

    // About thirty degrees, which is a firm lean and not a wild one.
    CHECK_NEAR(tiltFor(4.9f), -2.94f, 0.0001f);
}

TEST(the_filter_is_a_fifth_of_the_reading_exactly_as_it_shipped) {
    // mPrevTiltValueLowPass is declared, read, and never assigned in the
    // original, so the 0.8 term is always zero. Kept rather than repaired: a
    // working filter converges on the whole reading, which at three units of
    // offset per unit of acceleration would throw the wall five times further
    // than it ever went on a device.
    //
    // This test is here to catch someone "fixing" it. If the filter is ever
    // made to work, the -3.0 has to come down with it.
    const float reading = 8.0f;
    CHECK_NEAR(tiltFor(reading), -3.0f * 0.2f * reading, 0.0001f);
}

TEST(a_repeated_reading_does_not_wind_up) {
    // A consequence of the above worth pinning: because nothing accumulates,
    // the same reading gives the same lean however many times it arrives. A
    // real filter would ramp towards the full value instead.
    Rig rig;
    for (int i = 0; i < 20; ++i) {
        rig.processor.onSensorChanged(nullptr, 5.0f, 0.0f, 0.0f, GridLayer::STATE_MEDIA_SETS);
    }
    CHECK_NEAR(rig.camera.mEyeOffsetX, -3.0f, 0.0001f);
}

TEST(fullscreen_does_not_lean) {
    // A photo being looked at holds still.
    CHECK_NEAR(tiltFor(5.0f, GridLayer::STATE_FULL_SCREEN), 0.0f, 0.0001f);
}

TEST(the_other_states_do_lean) {
    CHECK_NEAR(tiltFor(5.0f, GridLayer::STATE_MEDIA_SETS), -3.0f, 0.0001f);
    CHECK_NEAR(tiltFor(5.0f, GridLayer::STATE_GRID_VIEW), -3.0f, 0.0001f);
    CHECK_NEAR(tiltFor(5.0f, GridLayer::STATE_TIMELINE), -3.0f, 0.0001f);
}
