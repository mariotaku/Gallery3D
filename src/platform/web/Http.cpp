#include "media/Http.h"

#include <memory>

#include <SDL3/SDL.h>
#include <emscripten/fetch.h>

namespace Http {

const char *userAgent() {
    // A page cannot choose. The browser sets User-Agent itself and forbids the
    // page from touching it.
    return "";
}

namespace {

// One request in flight. The callback has to outlive the call that started it.
struct PendingFetch {
    Http::Callback done;
};

void onFetchSucceeded(emscripten_fetch_t *fetch) {
    std::unique_ptr<PendingFetch> pending((PendingFetch *)fetch->userData);
    std::vector<uint8_t> bytes;
    if (fetch->numBytes > 0) {
        const uint8_t *data = (const uint8_t *)fetch->data;
        bytes.assign(data, data + fetch->numBytes);
    }
    const bool ok = (fetch->status == 200) && !bytes.empty();
    emscripten_fetch_close(fetch);
    if (pending->done) {
        pending->done(ok, std::move(bytes));
    }
}

void onFetchFailed(emscripten_fetch_t *fetch) {
    std::unique_ptr<PendingFetch> pending((PendingFetch *)fetch->userData);
    SDL_Log("http: %s: HTTP %d", fetch->url, (int)fetch->status);
    emscripten_fetch_close(fetch);
    if (pending->done) {
        pending->done(false, std::vector<uint8_t>());
    }
}

}  // namespace

void getAsync(const std::string &url, Callback done) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    SDL_strlcpy(attr.requestMethod, "GET", sizeof(attr.requestMethod));
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onFetchSucceeded;
    attr.onerror = onFetchFailed;
    attr.userData = new PendingFetch{std::move(done)};
    // Omit AIC-User-Agent to avoid CORS preflight; see Http.h.
    emscripten_fetch(&attr, url.c_str());
}

bool get(const std::string &url, std::vector<uint8_t> *out) {
    out->clear();

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    SDL_strlcpy(attr.requestMethod, "GET", sizeof(attr.requestMethod));
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;

    // Added headers trigger CORS preflight, which the image server rejects with 403.
    // A simple request succeeds with the browser's Referer and Access-Control-Allow-Origin.
    emscripten_fetch_t *fetch = emscripten_fetch(&attr, url.c_str());
    if (fetch == nullptr) {
        return false;
    }
    const bool ok = (fetch->status == 200) && (fetch->numBytes > 0);
    if (ok) {
        const uint8_t *bytes = (const uint8_t *)fetch->data;
        out->assign(bytes, bytes + fetch->numBytes);
    } else {
        SDL_Log("http: %s: HTTP %d", url.c_str(), (int)fetch->status);
    }
    emscripten_fetch_close(fetch);
    return ok;
}

}  // namespace Http
