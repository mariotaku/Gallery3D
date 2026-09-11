#include "Http.h"

#include <mutex>

#include <SDL3/SDL.h>

// Who is asking. The docs ask callers to identify themselves, and on native the
// image server enforces it: without this header every IIIF request is a 403,
// while the json endpoints serve anyone.
#define kUserAgent "gallery3d-sdl (git@mariotaku.me)"

#if defined(__EMSCRIPTEN__)

#include <emscripten/fetch.h>

namespace Http {

const char *userAgent() {
    // A page cannot choose. The browser sets User-Agent itself and forbids the
    // page from touching it.
    return "";
}

bool get(const std::string &url, std::vector<uint8_t> *out) {
    out->clear();

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    SDL_strlcpy(attr.requestMethod, "GET", sizeof(attr.requestMethod));
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;

    // Deliberately no AIC-User-Agent here, though the native side sends one.
    //
    // Any header a page adds turns the request into a preflighted one, and the
    // image server answers the preflight with a 403, so the GET never happens.
    // With no added header this is a simple request, and the Referer the
    // browser sends on its own is enough: the server answers 200 with
    // Access-Control-Allow-Origin, which is also what lets the result reach a
    // WebGL texture without tainting it.
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

// One curl handle per thread, kept open for the life of it.
//
// This matters more than it looks. A fresh handle means a fresh DNS lookup and
// a fresh TLS handshake for every single image, and the texture threads fetch
// hundreds. Reusing the handle keeps the connection, the TLS session and the
// resolved address, so the second image onward costs one round trip instead of
// four. curl_easy_reset clears the options and keeps all of that.
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
    // A thread parked on a dead connection is one that is not fetching
    // anything. Give up on a stalled transfer rather than waiting out the whole
    // timeout: under a kilobyte a second for five seconds is not coming back.
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
