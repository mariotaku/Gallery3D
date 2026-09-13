// Pushing the wall past either end of its travel.
//
// The wall stops at its first and last column, but a scroll that asks to go
// further is answered rather than swallowed: the camera's eye slides sideways
// while its look-at stays put, which leans the whole wall, and it swings back
// once the scrolling stops. The lean is the gap between the two.
#include "tests.h"

#include <cmath>

#include "grid/GridCamera.h"
#include "core/Vector3f.h"

namespace {

// A wall several windows wide, so there is somewhere to scroll to and two ends
// to run into.
const Vector3f kFirstSlot(0.0f, 0.0f, 0.0f);
const Vector3f kLastSlot(5000.0f, 0.0f, 0.0f);

GridCamera wallCamera() {
    return GridCamera(1280, 800, 96, 72);
}

// One wheel tick: ask to move, then answer being past the end. moveTo caps a
// single step at twice the window, so reaching an end takes several, the same
// as it does with a hand on the wheel.
void scrollTicks(GridCamera &camera, float perTick, int ticks, bool feedback = true) {
    for (int i = 0; i < ticks; ++i) {
        camera.moveBy(perTick, 0.0f, 0.0f);
        camera.computeConstraints(false, feedback, kFirstSlot, kLastSlot);
    }
}

// Lets the animation run out, the way frames do.
void settle(GridCamera &camera, int frames = 40) {
    for (int i = 0; i < frames; ++i) {
        camera.update(0.05f);
    }
}

float lean(const GridCamera &camera) {
    return std::fabs(camera.mEyeX - camera.mLookAtX);
}

}  // namespace

TEST(scrolling_back_from_the_first_column_leans_the_wall) {
    GridCamera camera = wallCamera();

    // Back past the start, which the wall cannot show.
    scrollTicks(camera, -30.0f, 4);
    settle(camera);

    CHECK(lean(camera) > 0.01f);
}

TEST(scrolling_on_past_the_last_column_leans_it_the_other_way) {
    GridCamera camera = wallCamera();

    // Forward off the far end, which takes a few ticks to reach.
    scrollTicks(camera, 30.0f, 6);
    settle(camera);

    CHECK(lean(camera) > 0.01f);
}

TEST(the_wall_swings_back_when_the_scrolling_stops) {
    GridCamera camera = wallCamera();

    scrollTicks(camera, -30.0f, 4);
    settle(camera);
    CHECK(lean(camera) > 0.01f);

    // Constraining is how the scroll says it is over.
    camera.computeConstraints(true, true, kFirstSlot, kLastSlot);
    settle(camera);
    CHECK(lean(camera) < 0.01f);
}

TEST(a_scroll_inside_the_wall_leans_nothing) {
    GridCamera camera = wallCamera();

    // A step the wall can answer, so there is nothing to push against.
    scrollTicks(camera, 1.0f, 1);
    settle(camera);

    CHECK(lean(camera) < 0.01f);
}

TEST(asking_for_no_feedback_leans_nothing) {
    GridCamera camera = wallCamera();

    // Fullscreen passes false: one photo fills the window and has no wall to
    // lean.
    scrollTicks(camera, -30.0f, 4, false);
    settle(camera);

    CHECK(lean(camera) < 0.01f);
}

TEST(the_wall_still_stops_at_its_first_column) {
    GridCamera camera = wallCamera();

    // The lean is feedback, not travel: the wall itself may not go past.
    camera.moveBy(-30.0f, 0.0f, 0.0f);
    const bool hitEdge = camera.computeConstraints(false, true, kFirstSlot, kLastSlot);
    settle(camera);

    CHECK(hitEdge);
    CHECK(camera.mLookAtX >= GridCamera::EYE_X - 0.01f);
}
