// Whether the app is in the background, for the threads that load photos.
//
// Android freezes an app it has put in the background, and kills one whose
// calls into another app's provider are still running when it tries. So the
// loaders stop taking work while paused, and a load that failed across a pause
// is taken for a cancelled one rather than a photo that cannot be read.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

namespace AppPause {

namespace detail {

inline std::atomic<bool> &pausedFlag() {
    static std::atomic<bool> paused{false};
    return paused;
}

inline std::atomic<uint64_t> &epochCounter() {
    static std::atomic<uint64_t> epoch{0};
    return epoch;
}

inline std::mutex &mutex() {
    static std::mutex lock;
    return lock;
}

inline std::condition_variable &condition() {
    static std::condition_variable wake;
    return wake;
}

}  // namespace detail

inline bool paused() {
    return detail::pausedFlag().load();
}

// Goes up each time the app pauses. Work that read another value when it
// started overlapped a pause, and its failure may be the cancellation.
inline uint64_t epoch() {
    return detail::epochCounter().load();
}

inline void setPaused(bool paused) {
    {
        std::lock_guard<std::mutex> lock(detail::mutex());
        if (paused && !detail::pausedFlag().load()) {
            detail::epochCounter().fetch_add(1);
        }
        detail::pausedFlag().store(paused);
    }
    detail::condition().notify_all();
}

// Blocks while the app is paused. Wakes a few times a second to ask stop
// whether to give up, so a shutdown is not held until the app comes back.
inline void waitUntilResumed(const std::function<bool()> &stop) {
    std::unique_lock<std::mutex> lock(detail::mutex());
    while (detail::pausedFlag().load() && !(stop && stop())) {
        detail::condition().wait_for(lock, std::chrono::milliseconds(200));
    }
}

}  // namespace AppPause
