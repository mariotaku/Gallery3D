// What order an album's photos come in.
//
// The original ended every query with DATE_TAKEN ASC, so a set was sorted
// before anything drew it, and both the time bar and the clusterer read the
// sequence as a timeline. A source here hands over whatever order it has - a
// directory listing, an api's idea of relevance - so the sort has to be done
// rather than assumed.
#include "tests.h"

#include <memory>
#include <vector>

#include "Dates.h"
#include "MediaItem.h"
#include "MediaSet.h"

namespace {

// Adds an item with the given date and caption, so the order can be read back.
void add(MediaSet &set, int64_t dateMs, const char *caption) {
    auto item = std::make_unique<MediaItem>();
    item->mDateTakenInMs = dateMs;
    item->mCaption = caption;
    set.addItem(std::move(item));
}

std::string order(const MediaSet &set) {
    std::string out;
    for (const MediaItem *item : set.getItems()) {
        if (!out.empty()) {
            out += ",";
        }
        out += item->mCaption;
    }
    return out;
}

}  // namespace

TEST(an_album_reads_oldest_first) {
    MediaSet set;
    add(set, 3000, "c");
    add(set, 1000, "a");
    add(set, 2000, "b");
    set.sortItemsByDate();
    CHECK(order(set) == std::string("a,b,c"));
}

TEST(dates_before_1970_sort_before_dates_after_it) {
    // A museum's catalogue is mostly negative timestamps. Anything comparing
    // these as unsigned, or clamping them at the epoch, puts a Degas after a
    // photograph from last year.
    MediaSet set;
    add(set, 1000LL * 60 * 60 * 24 * 365 * 30, "1999");
    add(set, -2500000000000LL, "1890");
    add(set, 0 - 1, "just before 1970");
    set.sortItemsByDate();
    CHECK(order(set) == std::string("1890,just before 1970,1999"));
}

TEST(undated_items_go_last_and_keep_their_order) {
    // Zero means the date is unknown, not 1970. Sorting on the value alone
    // would put every undated artwork ahead of every dated one, which for this
    // catalogue is most of the wall.
    MediaSet set;
    add(set, 0, "unknown one");
    add(set, 2000, "dated late");
    add(set, 0, "unknown two");
    add(set, -5000, "dated early");
    set.sortItemsByDate();
    CHECK(order(set) == std::string("dated early,dated late,unknown one,unknown two"));
}

TEST(sorting_twice_changes_nothing) {
    // It is called once per batch, and a batch may arrive after the set has
    // already been sorted once.
    MediaSet set;
    add(set, 30, "c");
    add(set, 10, "a");
    add(set, 20, "b");
    set.sortItemsByDate();
    const std::string once = order(set);
    set.sortItemsByDate();
    CHECK(order(set) == once);
}

TEST(items_added_after_a_sort_land_in_the_right_place) {
    // The museum's albums fill in twice: covers first, then the rest when the
    // album is opened.
    MediaSet set;
    add(set, 100, "cover");
    set.sortItemsByDate();

    add(set, 50, "earlier");
    add(set, 150, "later");
    set.sortItemsByDate();
    CHECK(order(set) == std::string("earlier,cover,later"));
}

TEST(an_empty_set_sorts_without_complaint) {
    MediaSet set;
    set.sortItemsByDate();
    CHECK_EQ(set.getNumItems(), 0);
}

// ---------------------------------------------------------------------------
// Turning a catalogue's year into a timestamp
// ---------------------------------------------------------------------------

TEST(a_year_becomes_the_first_of_january_utc) {
    // Checked against real calendar arithmetic, not against itself.
    CHECK_EQ(Dates::startOfYearMs(1970), 0LL);
    CHECK_EQ(Dates::startOfYearMs(1971), 31536000000LL);
    CHECK_EQ(Dates::startOfYearMs(2000), 946684800000LL);
    CHECK_EQ(Dates::startOfYearMs(2013), 1356998400000LL);
}

TEST(years_before_1970_go_negative_and_stay_exact) {
    // Most of a museum lives here. Counting 365 days to the year drifts about
    // three weeks a century, which is invisible in a sort and obvious on a
    // label: an artwork from 1982 used to read "Dec 29 1981".
    CHECK_EQ(Dates::startOfYearMs(1900), -2208988800000LL);
    CHECK_EQ(Dates::startOfYearMs(1889), -2556057600000LL);
    CHECK_EQ(Dates::startOfYearMs(1839), -4133980800000LL);
    CHECK_EQ(Dates::startOfYearMs(1600), -11676096000000LL);
}

TEST(the_century_rule_is_respected) {
    // 1900 was not a leap year and 2000 was, which is the case a naive
    // every-fourth-year calculation gets wrong.
    const int64_t day = 24LL * 3600LL * 1000LL;
    CHECK_EQ(Dates::startOfYearMs(1901) - Dates::startOfYearMs(1900), 365LL * day);
    CHECK_EQ(Dates::startOfYearMs(2001) - Dates::startOfYearMs(2000), 366LL * day);
    CHECK_EQ(Dates::startOfYearMs(1905) - Dates::startOfYearMs(1904), 366LL * day);
}

TEST(consecutive_years_always_move_forward) {
    // The sort depends on this and nothing else.
    for (int year = 1200; year <= 2100; ++year) {
        if (Dates::startOfYearMs(year) >= Dates::startOfYearMs(year + 1)) {
            CHECK(false);
            return;
        }
    }
    CHECK(true);
}

TEST(a_timestamp_becomes_a_date_for_any_year) {
    // The round trip the time bar and the cluster captions depend on.
    for (int year : {1970, 2013, 1900, 1889, 1839, 1600, 1, 0, -44}) {
        const Dates::Civil date = Dates::civilFromMs(Dates::startOfYearMs(year));
        CHECK_EQ(date.year, year);
        CHECK_EQ(date.month, 1);
        CHECK_EQ(date.day, 1);
    }
}

TEST(the_instant_before_1970_is_the_last_day_of_1969) {
    // Floor division, not truncation. Truncating puts every negative fraction
    // of a day on the wrong side of midnight.
    const Dates::Civil date = Dates::civilFromMs(-1);
    CHECK_EQ(date.year, 1969);
    CHECK_EQ(date.month, 12);
    CHECK_EQ(date.day, 31);
}

TEST(a_known_date_breaks_down_correctly) {
    // 2013-03-15T12:00:00Z, checked against a calendar rather than against the
    // code that produced it.
    const Dates::Civil date = Dates::civilFromMs(1363348800000LL);
    CHECK_EQ(date.year, 2013);
    CHECK_EQ(date.month, 3);
    CHECK_EQ(date.day, 15);
}

TEST(years_before_the_common_era_are_labelled_bc) {
    // ISO counts 1 BC as year zero, so a label is one more than the negation.
    // Without this an artefact from 44 BC reads "year -43".
    CHECK(Dates::yearLabel(1839) == std::string("1839"));
    CHECK(Dates::yearLabel(1) == std::string("1"));
    CHECK(Dates::yearLabel(0) == std::string("1 BC"));
    CHECK(Dates::yearLabel(-43) == std::string("44 BC"));
}

TEST(month_names_are_bounded) {
    CHECK(Dates::monthAbbreviation(1) == std::string("Jan"));
    CHECK(Dates::monthAbbreviation(12) == std::string("Dec"));
    // A -1 out of a failed localtime used to reach strftime and fail-fast the
    // process. Nothing here may do worse than return nothing.
    CHECK(Dates::monthAbbreviation(0) == std::string(""));
    CHECK(Dates::monthAbbreviation(13) == std::string(""));
    CHECK(Dates::monthAbbreviation(-1) == std::string(""));
}

// ---------------------------------------------------------------------------
// How much of a date is known
// ---------------------------------------------------------------------------

TEST(a_set_takes_the_coarsest_precision_it_holds) {
    // A caption can only be as precise as its vaguest member. One artwork known
    // to the year is enough to stop the whole cluster claiming a month.
    MediaSet set;
    {
        auto exact = std::make_unique<MediaItem>();
        exact->mDateTakenInMs = Dates::startOfYearMs(1990);
        exact->mDatePrecision = MediaItem::PRECISION_DAY;
        set.addItem(std::move(exact));
    }
    CHECK_EQ(set.datePrecision(), (int)MediaItem::PRECISION_DAY);

    {
        auto vague = std::make_unique<MediaItem>();
        vague->mDateTakenInMs = Dates::startOfYearMs(1889);
        vague->mDatePrecision = MediaItem::PRECISION_YEAR;
        set.addItem(std::move(vague));
    }
    CHECK_EQ(set.datePrecision(), (int)MediaItem::PRECISION_YEAR);
}

TEST(an_undated_item_does_not_coarsen_the_set) {
    // Precision is only meaningful for an item that has a date at all.
    MediaSet set;
    {
        auto exact = std::make_unique<MediaItem>();
        exact->mDateTakenInMs = Dates::startOfYearMs(1990);
        set.addItem(std::move(exact));
    }
    {
        auto undated = std::make_unique<MediaItem>();
        undated->mDateTakenInMs = 0;
        undated->mDatePrecision = MediaItem::PRECISION_YEAR;
        set.addItem(std::move(undated));
    }
    CHECK_EQ(set.datePrecision(), (int)MediaItem::PRECISION_DAY);
}

TEST(a_photo_keeps_the_precision_it_always_had) {
    // Nothing that writes a real timestamp had to be changed, so the default
    // has to be the precise one.
    MediaItem item;
    CHECK_EQ(item.mDatePrecision, (int)MediaItem::PRECISION_DAY);
}
