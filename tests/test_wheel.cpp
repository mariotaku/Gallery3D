// Pinching with a mouse wheel.
//
// A wheel reports one tick at a time and never says when the hand stopped, so
// the detector has to decide that for itself. Getting it wrong is visible: a
// gesture that ends on every tick snaps the wall back before the next tick
// continues it.
#include "tests.h"

#include <vector>

#include "GestureDetector.h"

namespace {

// Records what the detector reports, in order.
struct Recorder : ScaleGestureDetector::Listener {
    std::vector<std::string> calls;
    std::vector<float> scaleFactors;
    float spanAtEnd = 0.0f;

    bool onScale(ScaleGestureDetector *detector) override {
        calls.push_back("scale");
        scaleFactors.push_back(detector->getScaleFactor());
        return true;
    }
    bool onScaleBegin(ScaleGestureDetector *detector) override {
        (void)detector;
        calls.push_back("begin");
        return true;
    }
    void onScaleEnd(ScaleGestureDetector *detector, bool cancel) override {
        (void)cancel;
        calls.push_back("end");
        spanAtEnd = detector->getCurrentSpan();
    }

    int count(const char *name) const {
        int total = 0;
        for (const std::string &call : calls) {
            if (call == name) {
                ++total;
            }
        }
        return total;
    }
};

// Longer than the detector waits for another tick.
const float kPastIdle = 0.5f;

}  // namespace

TEST(one_wheel_tick_opens_a_gesture_without_closing_it) {
    Recorder recorder;
    ScaleGestureDetector detector(&recorder);

    detector.onWheel(100.0f, 100.0f, 1.0f);

    CHECK_EQ(recorder.count("begin"), 1);
    CHECK_EQ(recorder.count("scale"), 1);
    CHECK_EQ(recorder.count("end"), 0);
    CHECK(detector.isInProgress());
}

TEST(ticks_in_a_row_build_on_one_another) {
    Recorder recorder;
    ScaleGestureDetector detector(&recorder);

    for (int i = 0; i < 3; ++i) {
        detector.onWheel(100.0f, 100.0f, 1.0f);
    }

    // One gesture, not three.
    CHECK_EQ(recorder.count("begin"), 1);
    CHECK_EQ(recorder.count("scale"), 3);
    CHECK_EQ(recorder.count("end"), 0);

    // The span carries across ticks: three 12 percent steps off the 200 seed.
    CHECK_NEAR(detector.getCurrentSpan(), 200.0f * 1.12f * 1.12f * 1.12f, 0.01f);
}

TEST(a_gesture_ends_once_the_ticks_stop) {
    Recorder recorder;
    ScaleGestureDetector detector(&recorder);

    detector.onWheel(100.0f, 100.0f, 1.0f);
    detector.update(kPastIdle);

    CHECK_EQ(recorder.count("end"), 1);
    CHECK(!detector.isInProgress());
}

TEST(a_gesture_survives_the_gap_between_ticks) {
    Recorder recorder;
    ScaleGestureDetector detector(&recorder);

    detector.onWheel(100.0f, 100.0f, 1.0f);
    // A wheel turned by hand reports well inside the idle window.
    detector.update(0.05f);
    detector.onWheel(100.0f, 100.0f, 1.0f);
    detector.update(0.05f);

    CHECK_EQ(recorder.count("begin"), 1);
    CHECK_EQ(recorder.count("end"), 0);

    // And the wait starts again from the last tick, not from the first.
    detector.update(kPastIdle);
    CHECK_EQ(recorder.count("end"), 1);
    CHECK_NEAR(recorder.spanAtEnd, 200.0f * 1.12f * 1.12f, 0.01f);
}

TEST(the_next_gesture_starts_from_the_seed_again) {
    Recorder recorder;
    ScaleGestureDetector detector(&recorder);

    detector.onWheel(100.0f, 100.0f, 1.0f);
    detector.update(kPastIdle);
    detector.onWheel(100.0f, 100.0f, 1.0f);

    CHECK_EQ(recorder.count("begin"), 2);
    CHECK_NEAR(detector.getCurrentSpan(), 200.0f * 1.12f, 0.01f);
}

TEST(scrolling_back_shrinks_the_span) {
    Recorder recorder;
    ScaleGestureDetector detector(&recorder);

    detector.onWheel(100.0f, 100.0f, -1.0f);

    CHECK(detector.getScaleFactor() < 1.0f);
    CHECK_NEAR(detector.getCurrentSpan(), 200.0f / 1.12f, 0.01f);
}

TEST(an_idle_update_without_a_gesture_reports_nothing) {
    Recorder recorder;
    ScaleGestureDetector detector(&recorder);

    detector.update(kPastIdle);

    CHECK_EQ(recorder.calls.size(), (size_t)0);
}
