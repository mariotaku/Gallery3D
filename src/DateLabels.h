// Timestamps as text, for anything that shows a date to a reader.
//
// Split out because two places need the same answer: the clusterer names a
// bucket after the span it covers, and the details sheet says when one photo
// was taken. A date written two ways in one window is a date the reader has to
// reconcile.
#pragma once

#include <cstdint>
#include <string>

#include "Dates.h"

namespace DateLabels {

// Breaks a timestamp into a date.
//
// Local time where the C library can answer, because that is how a camera wrote
// it, and this is the case every photograph falls into.
//
// Its own arithmetic otherwise. localtime refuses anything before 1970, and on
// Windows it does so by filling the tm with -1 and returning an error, which is
// a date of month -1 to anything that does not check. A museum's catalogue is
// almost entirely before 1970.
Dates::Civil dateOf(int64_t millis);

std::string dayMonthYear(int64_t millis);
std::string dayMonth(int64_t millis);
std::string monthYear(int64_t millis);

// Sortable, for deciding whether two instants share a day or a year.
int64_t dayKey(int64_t millis);
int yearOf(int64_t millis);

// A date said only as precisely as it is known. `precision` is one of
// MediaItem's DatePrecision values: a catalogue that recorded a year alone is
// stored as the first of January, and printing "01 Jan" invents a day nobody
// wrote down.
std::string atPrecision(int64_t millis, int precision);

}  // namespace DateLabels
