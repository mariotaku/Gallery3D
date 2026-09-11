// HTTP via native libcurl or browser Fetch.
// Native IIIF requires AIC-User-Agent; browser requests omit it because its CORS
// preflight returns 403. The browser's Referer is sufficient for a simple GET.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Http {

// Fills `out` with the body, or returns false and leaves it empty. Blocks, so
// call it from a loader thread rather than the render thread. Not available on
// the web, where nothing may block.
bool get(const std::string &url, std::vector<uint8_t> *out);

// Called with the body, or with false and nothing on failure.
using Callback = std::function<void(bool ok, std::vector<uint8_t> bytes)>;

// Native: blocks on the calling thread and invokes the callback before returning.
// Web: returns immediately and invokes the callback later.
void getAsync(const std::string &url, Callback done);

// Who to say we are, where the platform lets us choose. Empty when it does not.
const char *userAgent();

}  // namespace Http
