// LocalTime on every platform: the same moments break down, and the same ones
// do not, whatever the C library underneath would accept.
#include "tests.h"

#include <ctime>

#include "core/LocalTime.h"

TEST(local_time_answers_from_the_epoch_to_the_end_of_the_year_3000) {
    std::tm parts {};
    CHECK(LocalTime::of(0, parts));
    CHECK(LocalTime::of((std::time_t)LocalTime::kLatest, parts));
    CHECK(!LocalTime::of(-1, parts));
    CHECK(!LocalTime::of((std::time_t)(LocalTime::kLatest + 1), parts));
}

TEST(local_time_breaks_a_moment_into_its_calendar) {
    // 2025-09-15 12:00:00 UTC. The zone is the machine's, so only what no zone
    // moves is checked exactly, and the day to within one either side.
    std::tm parts {};
    CHECK(LocalTime::of((std::time_t)1757937600LL, parts));
    CHECK_EQ(parts.tm_year, 125);
    CHECK_EQ(parts.tm_mon, 8);
    CHECK(parts.tm_mday >= 14 && parts.tm_mday <= 16);
    CHECK_EQ(parts.tm_sec, 0);
}
