// One blocking luna call. webOS answers a service call on a main loop of its
// own, and everything this app asks of a service it asks from a scan that is
// already off the render thread, so the call waits for its reply rather than
// threading a callback back through the scan.
#pragma once

#include <string>

namespace Luna {

// Sends payload to uri and returns the reply's payload, or an empty string if
// the call could not be sent or no reply arrived in time.
std::string call(const std::string &uri, const std::string &payload, int timeoutMs = 5000);

}  // namespace Luna
