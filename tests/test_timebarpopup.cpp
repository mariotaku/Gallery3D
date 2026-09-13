// The date shown over the scroll knob.
//
// Dates::Civil counts months from one and the name table is indexed from zero,
// which the time bar kept its own copy of and read straight. Every month came
// out as the next one along, and December read one past the end of the table
// and took the process with it.
#include "tests.h"

#include <string>

#include "hud/TimeBar.h"

namespace {

std::string popupFor(int year, int month, int day) {
    return TimeBar::popupTextFor(year, month, day);
}

}  // namespace

TEST(the_popup_names_the_month_it_was_given) {
    CHECK(popupFor(2026, 1, 5) == "Jan 5 2026");
    CHECK(popupFor(2026, 6, 21) == "Jun 21 2026");
    CHECK(popupFor(2026, 11, 30) == "Nov 30 2026");
}

TEST(december_is_a_month_like_any_other) {
    // Twelve is the last month, not one past the last name.
    CHECK(popupFor(2026, 12, 25) == "Dec 25 2026");
}

TEST(a_month_out_of_range_is_answered_rather_than_read) {
    // Nothing here may index a table with it.
    CHECK(popupFor(2026, 13, 1) == "Date unknown");
    CHECK(popupFor(2026, 0, 1) == "Date unknown");
    CHECK(popupFor(2026, -3, 1) == "Date unknown");
}

TEST(a_date_at_or_before_the_epoch_is_no_date_at_all) {
    // Which is what a file with nothing readable in it comes out as.
    CHECK(popupFor(1970, 6, 1) == "Date unknown");
    CHECK(popupFor(1969, 6, 1) == "Date unknown");
}

TEST(the_day_is_carried_through_as_written) {
    CHECK(popupFor(2026, 3, 1) == "Mar 1 2026");
    CHECK(popupFor(2026, 3, 31) == "Mar 31 2026");
}
