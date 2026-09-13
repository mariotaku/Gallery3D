#include "core/LocalTime.h"

namespace LocalTime {

bool of(std::time_t seconds, std::tm &parts) {
    return localtime_s(&parts, &seconds) == 0;
}

}  // namespace LocalTime
