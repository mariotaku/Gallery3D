#include "Http.h"

#include <mutex>

#include <SDL3/SDL.h>

// Native IIIF requires AIC-User-Agent identification; missing it returns 403.
#define kUserAgent "gallery3d-sdl (git@mariotaku.me)"

#if defined(__EMSCRIPTEN__)

#include <emscripten/fetch.h>

#include <memory>

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

#else

#include <curl/curl.h>

namespace {

size_t appendToVector(void *data, size_t size, size_t count, void *userData) {
    const size_t total = size * count;
    std::vector<uint8_t> *out = (std::vector<uint8_t> *)userData;
    const uint8_t *bytes = (const uint8_t *)data;
    out->insert(out->end(), bytes, bytes + total);
    return total;
}

// One curl handle per thread preserves DNS, TLS sessions and connections across requests.
// curl_easy_reset clears options while retaining those caches.
CURL *threadHandle() {
    struct Holder {
        CURL *handle = curl_easy_init();
        ~Holder() {
            if (handle != nullptr) {
                curl_easy_cleanup(handle);
            }
        }
    };
    static thread_local Holder holder;
    if (holder.handle != nullptr) {
        curl_easy_reset(holder.handle);
    }
    return holder.handle;
}

}  // namespace

namespace Http {

const char *userAgent() {
    return kUserAgent;
}

void getAsync(const std::string &url, Callback done) {
    // Native callbacks run inline on the requesting loader thread.
    std::vector<uint8_t> bytes;
    const bool ok = get(url, &bytes);
    if (done) {
        done(ok, std::move(bytes));
    }
}

bool get(const std::string &url, std::vector<uint8_t> *out) {
    // curl's global state is not safe to set up lazily from several threads.
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL *handle = threadHandle();
    if (handle == nullptr) {
        return false;
    }
    out->clear();
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendToVector);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    // Abort transfers below one kilobyte per second for five seconds.
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 5L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, kUserAgent);
    // Required here, unlike in the browser: the image server 403s every IIIF
    // request without it. A plain User-Agent does not do.
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "AIC-User-Agent: " kUserAgent);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);

    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    // The handle stays; only the header list is ours to free.
    curl_slist_free_all(headers);

    if (result != CURLE_OK) {
        SDL_Log("http: %s: %s", url.c_str(), curl_easy_strerror(result));
        return false;
    }
    if (status != 200) {
        SDL_Log("http: %s: HTTP %ld", url.c_str(), status);
        return false;
    }
    return true;
}

}  // namespace Http

#endif
