#include "Luna.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <glib.h>
#include <luna-service2/lunaservice.h>

#include <SDL3/SDL.h>

namespace {

// The bus connection and the loop that carries its replies. One per process,
// opened the first time something is asked and left open afterwards.
//
// This owns its loop rather than joining one of SDL's. libhelpers, which SDL
// calls the bus through, dispatches a reply on a loop this app never runs, so a
// call made through it from a thread that is waiting for the answer is never
// answered. The startup scan is exactly that: it blocks until it knows where
// the photos are.
struct Bus {
    LSHandle *handle = nullptr;
    GMainContext *context = nullptr;
    GMainLoop *loop = nullptr;
    std::thread thread;

    ~Bus() {
        if (loop != nullptr) {
            g_main_loop_quit(loop);
        }
        if (thread.joinable()) {
            thread.join();
        }
        // The handle and the loop outlive the thread on purpose: unregistering
        // races the loop's last iteration, and the process is ending anyway.
    }
};

struct Reply {
    std::mutex mutex;
    std::condition_variable arrived;
    bool replied = false;
    std::string payload;
};

bool onReply(LSHandle *handle, LSMessage *message, void *context) {
    (void)handle;
    Reply *reply = static_cast<Reply *>(context);
    const char *payload = LSMessageGetPayload(message);
    {
        std::lock_guard<std::mutex> lock(reply->mutex);
        reply->payload = payload != nullptr ? payload : std::string();
        reply->replied = true;
    }
    reply->arrived.notify_one();
    return true;
}

Bus *bus() {
    static Bus *opened = [] {
        Bus *made = new Bus();
        LSError error;
        LSErrorInit(&error);
        // The app's own name first: installing the app writes a bus role under
        // it, and the database grants a named caller more than an unnamed one.
        // SDL may already hold it, because it registers the app for its
        // lifecycle events and a name may be claimed once, so fall back to the
        // anonymous connection the same role allows.
        const char *appId = SDL_getenv("APPID");
        bool opened = appId != nullptr && LSRegister(appId, &made->handle, &error);
        if (!opened) {
            // LSError carries the first failure, and LSRegister does not clear
            // it, so a second attempt would report the first one's message.
            LSErrorFree(&error);
            LSErrorInit(&error);
            opened = LSRegister(nullptr, &made->handle, &error);
        }
        if (!opened) {
            SDL_Log("luna: cannot open the bus: %s", error.message ? error.message : "unknown");
            LSErrorFree(&error);
            made->handle = nullptr;
            return made;
        }
        made->context = g_main_context_new();
        made->loop = g_main_loop_new(made->context, FALSE);
        if (!LSGmainContextAttach(made->handle, made->context, &error)) {
            SDL_Log("luna: cannot attach the bus: %s", error.message ? error.message : "unknown");
            LSErrorFree(&error);
            made->handle = nullptr;
            return made;
        }
        made->thread = std::thread([made] { g_main_loop_run(made->loop); });
        return made;
    }();
    return opened;
}

}  // namespace

namespace Luna {

std::string call(const std::string &uri, const std::string &payload, int timeoutMs) {
    Bus *open = bus();
    if (open->handle == nullptr) {
        return std::string();
    }
    // The reply state outlives this function when the call times out: the
    // callback runs on the loop's thread and cancelling races a reply already
    // on its way. A timeout means the service is not answering at all, so a
    // call that gives up leaks the state rather than freeing what the loop may
    // still write to.
    Reply *reply = new Reply();
    LSError error;
    LSErrorInit(&error);
    LSMessageToken token = 0;
    if (!LSCallOneReply(open->handle, uri.c_str(), payload.c_str(), onReply, reply, &token,
                        &error)) {
        SDL_Log("luna: %s failed: %s", uri.c_str(), error.message ? error.message : "unknown");
        LSErrorFree(&error);
        delete reply;
        return std::string();
    }
    std::unique_lock<std::mutex> lock(reply->mutex);
    const bool arrived = reply->arrived.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                                 [reply] { return reply->replied; });
    if (!arrived) {
        lock.unlock();
        LSCallCancel(open->handle, token, nullptr);
        SDL_Log("luna: %s did not answer in %d ms", uri.c_str(), timeoutMs);
        return std::string();
    }
    std::string answer = std::move(reply->payload);
    lock.unlock();
    delete reply;
    return answer;
}

}  // namespace Luna
