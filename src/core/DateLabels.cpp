#include "core/DateLabels.h"

#include <ctime>

#include <SDL3/SDL.h>

#include "core/LocalTime.h"
#include "media/MediaItem.h"

namespace DateLabels {

Dates::Civil dateOf(int64_t millis) {
    const std::time_t seconds = (std::time_t)(millis / 1000);
    if (seconds >= 0) {
        std::tm parts {};
        if (LocalTime::of(seconds, parts)) {
            Dates::Civil civil;
            civil.year = parts.tm_year + 1900;
            civil.month = parts.tm_mon + 1;
            civil.day = parts.tm_mday;
            return civil;
        }
    }
    return Dates::civilFromMs(millis);
}

std::string dayMonthYear(int64_t millis) {
    const Dates::Civil date = dateOf(millis);
    char buffer[64];
    SDL_snprintf(buffer, sizeof(buffer), "%02d %s %s", date.day, Dates::monthAbbreviation(date.month),
                 Dates::yearLabel(date.year).c_str());
    return std::string(buffer);
}

std::string dayMonth(int64_t millis) {
    const Dates::Civil date = dateOf(millis);
    char buffer[32];
    SDL_snprintf(buffer, sizeof(buffer), "%02d %s", date.day, Dates::monthAbbreviation(date.month));
    return std::string(buffer);
}

std::string monthYear(int64_t millis) {
    const Dates::Civil date = dateOf(millis);
    char buffer[64];
    SDL_snprintf(buffer, sizeof(buffer), "%s %s", Dates::monthAbbreviation(date.month),
                 Dates::yearLabel(date.year).c_str());
    return std::string(buffer);
}

int64_t dayKey(int64_t millis) {
    const Dates::Civil date = dateOf(millis);
    return (int64_t)date.year * 10000 + date.month * 100 + date.day;
}

int yearOf(int64_t millis) {
    return dateOf(millis).year;
}

std::string atPrecision(int64_t millis, int precision) {
    if (precision >= MediaItem::PRECISION_YEAR) {
        return Dates::yearLabel(yearOf(millis));
    }
    if (precision >= MediaItem::PRECISION_MONTH) {
        return monthYear(millis);
    }
    return dayMonthYear(millis);
}

}  // namespace DateLabels
