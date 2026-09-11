// One blocking GET, however the platform does them.
//
// Native builds use libcurl. The web build cannot: a browser will not let a
// page open a socket, so the request goes through the Fetch API instead.
//
// The seam also carries a difference that is not just plumbing. The Art
// Institute's image server wants an AIC-User-Agent header naming the caller,
// and refuses the request without it - but it answers the CORS preflight for
// that header with a 403, so a browser sending it never gets as far as the GET.
// Without the header, the same browser request succeeds as a simple one, on the
// Referer it sends by itself. So the header is right on native and wrong on the
// web, and each side says so where it sets it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Http {

// Fills `out` with the body, or returns false and leaves it empty. Blocks, so
// call it from a loader thread rather than the render thread.
bool get(const std::string &url, std::vector<uint8_t> *out);

// Who to say we are, where the platform lets us choose. Empty when it does not.
const char *userAgent();

}  // namespace Http
