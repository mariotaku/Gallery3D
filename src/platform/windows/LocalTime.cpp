#include "core/LocalTime.h"

namespace LocalTime {

bool of(std::time_t seconds, std::tm &parts) {
    if (seconds < 0 || (long long)seconds > kLatest) {
        return false;
    }
    return localtime_s(&parts, &seconds) == 0;
}

}  // namespace LocalTime
