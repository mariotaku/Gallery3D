#include "core/LocalTime.h"

namespace LocalTime {

bool of(std::time_t seconds, std::tm &parts) {
    return localtime_r(&seconds, &parts) != nullptr;
}

}  // namespace LocalTime
