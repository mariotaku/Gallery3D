// Local time from the C library, which alone knows the time zone's rules. The
// thread-safe form of the call is spelled differently on Windows.
#pragma once

#include <ctime>

namespace LocalTime {

// Breaks seconds since the epoch into the user's local calendar and clock.
// False when the C library cannot, which on Windows includes any moment before
// 1970.
bool of(std::time_t seconds, std::tm &parts);

}  // namespace LocalTime
