// Telling a press apart from the slow start of a drag.
//
// A press is decided on a timer, before the finger has said whether it is
// going anywhere. Swiping in from the screen edge to go back often rests a
// moment on a thumbnail first, and the platform hands that touch to the wall
// whenever it decides the swipe is not its own. Without this, such a swipe
// selected whatever it started on and the rest of it was swallowed.
#include "tests.h"

#include <string>
#include <vector>

#include "grid/GestureDetector.h"
#include "app/Input.h"

namespace {

// Longer than the detector waits before calling a press a press.
const uint64_t kPastLongPress = 600;

struct Recorder : GestureDetector::Listener {
    std::vector<std::string> calls;

    bool onDown(const MotionEvent &) override {
        calls.push_back("down");
        return true;
    }
    bool onFling(const MotionEvent &, const MotionEvent &, float, float) override {
        calls.push_back("fling");
        return true;
    }
    void onLongPress(const MotionEvent &) override {
        calls.push_back("longpress");
    }
    void onLongPressCancelled() override {
        calls.push_back("longpress-cancelled");
    }
    bool onScroll(const MotionEvent &, const MotionEvent &, float, float) override {
        return true;
    }
    void onShowPress(const MotionEvent &) override {}
    bool onSingleTapUp(const MotionEvent &) override {
        calls.push_back("tap");
        return true;
    }
    bool onDoubleTap(const MotionEvent &) override {
        calls.push_back("doubletap");
        return true;
    }
    bool onDoubleTapEvent(const MotionEvent &) override {
        return true;
    }
    bool onSingleTapConfirmed(const MotionEvent &) override {
        return true;
    }

    bool saw(const char *name) const {
        for (const std::string &call : calls) {
            if (call == name) {
                return true;
            }
        }
        return false;
    }
};

MotionEvent touchAt(int action, float x, float y, uint64_t at) {
    MotionEvent event;
    event.action = action;
    event.xs[0] = x;
    event.ys[0] = y;
    event.eventTime = at;
    event.pointerCount = 1;
    return event;
}

}  // namespace

TEST(a_finger_that_stays_put_is_a_press) {
    Recorder recorder;
    GestureDetector detector(&recorder);
    detector.setIsLongpressEnabled(true);

    detector.onTouchEvent(touchAt(MotionEvent::ACTION_DOWN, 100.0f, 100.0f, 0));
    detector.update(kPastLongPress);

    CHECK(recorder.saw("longpress"));
    CHECK(!recorder.saw("longpress-cancelled"));
}

TEST(a_finger_that_moves_on_takes_the_press_back) {
    Recorder recorder;
    GestureDetector detector(&recorder);
    detector.setIsLongpressEnabled(true);

    // Rests on a thumbnail long enough to be called a press.
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_DOWN, 100.0f, 100.0f, 0));
    detector.update(kPastLongPress);
    CHECK(recorder.saw("longpress"));

    // Then carries on across the wall, which is what a back swipe does.
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_MOVE, 400.0f, 100.0f, kPastLongPress + 50));

    CHECK(recorder.saw("longpress-cancelled"));
}

TEST(a_swipe_that_began_as_a_press_ends_as_a_drag) {
    Recorder recorder;
    GestureDetector detector(&recorder);
    detector.setIsLongpressEnabled(true);

    detector.onTouchEvent(touchAt(MotionEvent::ACTION_DOWN, 100.0f, 100.0f, 0));
    detector.update(kPastLongPress);
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_MOVE, 400.0f, 100.0f, kPastLongPress + 50));
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_UP, 600.0f, 100.0f, kPastLongPress + 100));

    // The lift must not open whatever the finger started on.
    CHECK(!recorder.saw("tap"));
}

TEST(a_press_held_to_the_end_is_not_taken_back) {
    Recorder recorder;
    GestureDetector detector(&recorder);
    detector.setIsLongpressEnabled(true);

    detector.onTouchEvent(touchAt(MotionEvent::ACTION_DOWN, 100.0f, 100.0f, 0));
    detector.update(kPastLongPress);
    // Lifted where it landed, give or take a wobble inside the slop.
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_UP, 104.0f, 102.0f, kPastLongPress + 100));

    CHECK(recorder.saw("longpress"));
    CHECK(!recorder.saw("longpress-cancelled"));
    CHECK(!recorder.saw("tap"));
}

TEST(a_finger_that_never_rests_is_never_a_press) {
    Recorder recorder;
    GestureDetector detector(&recorder);
    detector.setIsLongpressEnabled(true);

    // Away before the timer, which is the brisk back swipe.
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_DOWN, 100.0f, 100.0f, 0));
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_MOVE, 400.0f, 100.0f, 80));
    detector.update(kPastLongPress);
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_UP, 600.0f, 100.0f, 120));

    CHECK(!recorder.saw("longpress"));
    CHECK(!recorder.saw("longpress-cancelled"));
    CHECK(!recorder.saw("tap"));
}

TEST(a_wobble_inside_the_slop_does_not_take_the_press_back) {
    Recorder recorder;
    GestureDetector detector(&recorder);
    detector.setIsLongpressEnabled(true);

    detector.onTouchEvent(touchAt(MotionEvent::ACTION_DOWN, 100.0f, 100.0f, 0));
    detector.update(kPastLongPress);
    // A held finger is never perfectly still.
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_MOVE, 108.0f, 105.0f, kPastLongPress + 40));

    CHECK(recorder.saw("longpress"));
    CHECK(!recorder.saw("longpress-cancelled"));
}

TEST(a_quick_tap_still_taps) {
    Recorder recorder;
    GestureDetector detector(&recorder);
    detector.setIsLongpressEnabled(true);

    detector.onTouchEvent(touchAt(MotionEvent::ACTION_DOWN, 100.0f, 100.0f, 0));
    detector.onTouchEvent(touchAt(MotionEvent::ACTION_UP, 101.0f, 100.0f, 60));

    CHECK(recorder.saw("tap"));
    CHECK(!recorder.saw("longpress"));
}
