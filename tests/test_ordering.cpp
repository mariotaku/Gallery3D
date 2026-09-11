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
