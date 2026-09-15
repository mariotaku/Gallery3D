// AppPause, which the photo loaders consult while the app is in the
// background: each pause moves the epoch on, and a wait returns once the app
// resumes or once it is told to stop.
#include "tests.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <thread>

#include "core/AppPause.h"

TEST(each_pause_moves_the_epoch_on_once) {
    const uint64_t before = AppPause::epoch();
    AppPause::setPaused(true);
    CHECK(AppPause::paused());
    CHECK(AppPause::epoch() == before + 1);
    // Told twice, still one pause.
    AppPause::setPaused(true);
    CHECK(AppPause::epoch() == before + 1);
    AppPause::setPaused(false);
    CHECK(!AppPause::paused());
    CHECK(AppPause::epoch() == before + 1);
}

TEST(a_paused_wait_returns_on_resume_or_when_told_to_stop) {
    AppPause::setPaused(true);
    std::atomic<bool> returned{false};
    std::thread waiter([&returned]() {
        AppPause::waitUntilResumed(nullptr);
        returned = true;
    });
    SDL_Delay(50);
    CHECK(!returned.load());
    AppPause::setPaused(false);
    waiter.join();
    CHECK(returned.load());

    AppPause::setPaused(true);
    std::atomic<bool> stop{false};
    std::thread stopped([&stop]() { AppPause::waitUntilResumed([&stop]() { return stop.load(); }); });
    stop = true;
    // Within one wake of the wait.
    stopped.join();
    AppPause::setPaused(false);
}
