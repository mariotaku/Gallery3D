// Local time from the C library, which alone knows the time zone's rules. The
// thread-safe form of the call is spelled differently on Windows.
#pragma once

#include <ctime>

namespace LocalTime {

// The last second of the year 3000 UTC, which is as far as Windows' C library
// goes, and so as far as any platform answers.
constexpr long long kLatest = 32535215999LL;

// Breaks seconds since the epoch into the user's local calendar and clock. The
// same moments answer on every platform: from the epoch to kLatest. False
// outside that, or when the C library cannot.
bool of(std::time_t seconds, std::tm &parts);

}  // namespace LocalTime
