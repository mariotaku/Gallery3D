// Date labels shared by clustering and media details.
#pragma once

#include <cstdint>
#include <string>

#include "Dates.h"

namespace DateLabels {

// Breaks a timestamp into local time when supported, otherwise UTC arithmetic.
// Windows localtime rejects pre-1970 timestamps and fills tm with -1 on failure.
Dates::Civil dateOf(int64_t millis);

std::string dayMonthYear(int64_t millis);
std::string dayMonth(int64_t millis);
std::string monthYear(int64_t millis);

// Sortable, for deciding whether two instants share a day or a year.
int64_t dayKey(int64_t millis);
int yearOf(int64_t millis);

// Formats only the known MediaItem::DatePrecision; year-only timestamps use January 1
// internally.
std::string atPrecision(int64_t millis, int precision);

}  // namespace DateLabels
