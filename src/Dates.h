// Gregorian calendar conversion between years and epoch milliseconds.
#pragma once

#include <cstdint>
#include <string>

namespace Dates {

// Civil date without a timezone database. Handles pre-1970 dates that localtime_s rejects.
struct Civil {
    int year = 1970;  // 0 is 1 BC, -1 is 2 BC, as ISO 8601 counts them
    int month = 1;    // 1 to 12
    int day = 1;      // 1 to 31
};

// January 1 at midnight UTC, in epoch milliseconds.
// Howard Hinnant's days_from_civil specialised to the first of the year.
inline int64_t startOfYearMs(int year) {
    // Use March-based years; January belongs to the preceding year.
    const int marchYear = year - 1;
    const int era = (marchYear >= 0 ? marchYear : marchYear - 399) / 400;
    const unsigned yearOfEra = (unsigned)(marchYear - era * 400);
    // January is month 11 of that year, which puts its first day 306 days in.
    const unsigned dayOfYear = 306;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const int64_t days = (int64_t)era * 146097 + (int64_t)dayOfEra - 719468;
    return days * 24LL * 3600LL * 1000LL;
}

// Inverse of startOfYearMs for any instant, in UTC. The caller selects local-time formatting.
inline Civil civilFromMs(int64_t ms) {
    const int64_t msPerDay = 24LL * 3600LL * 1000LL;
    // Floor division, because -1ms is the last day of 1969 and not the first
    // day of 1970.
    int64_t days = ms / msPerDay;
    if (ms % msPerDay != 0 && ms < 0) {
        --days;
    }

    // Howard Hinnant's civil_from_days.
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra = (unsigned)(days - era * 146097);
    const unsigned yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t year = (int64_t)yearOfEra + era * 400;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned monthPrime = (5 * dayOfYear + 2) / 153;

    Civil civil;
    civil.day = (int)(dayOfYear - (153 * monthPrime + 2) / 5 + 1);
    civil.month = (int)(monthPrime + (monthPrime < 10 ? 3 : -9));
    civil.year = (int)(year + (civil.month <= 2 ? 1 : 0));
    return civil;
}

inline const char *monthAbbreviation(int month) {
    static const char *const names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (month < 1 || month > 12) {
        return "";
    }
    return names[month - 1];
}

// "1839", or "44 BC". ISO counts 1 BC as year zero, so the label is one more
// than the negation.
inline std::string yearLabel(int year) {
    if (year > 0) {
        return std::to_string(year);
    }
    return std::to_string(1 - year) + " BC";
}

}  // namespace Dates
