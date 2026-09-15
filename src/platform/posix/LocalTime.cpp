#include "core/LocalTime.h"

namespace LocalTime {

bool of(std::time_t seconds, std::tm &parts) {
    // localtime_r answers before 1970 and past 3000 as well, which Windows
    // does not.
    if (seconds < 0 || (long long)seconds > kLatest) {
        return false;
    }
    return localtime_r(&seconds, &parts) != nullptr;
}

}  // namespace LocalTime
