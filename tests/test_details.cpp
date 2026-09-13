// The details sheet: what is known about what is selected, as lines of text.
//
// The shapes it has to tell apart are one item, several items, and whole
// albums, because the original says different things about each. Nothing here
// touches GL, so the strings can be checked directly.
#include "tests.h"

#include <memory>
#include <string>
#include <vector>

#include "core/Dates.h"
#include "media/MediaBucketList.h"
#include "media/MediaDetails.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"

namespace {

MediaItem *addItem(MediaSet &set, const char *caption, const char *mime, int64_t takenMs, int precision) {
    auto item = std::make_unique<MediaItem>();
    item->mCaption = caption;
    item->mMimeType = mime;
    item->mDateTakenInMs = takenMs;
    item->mDatePrecision = precision;
    MediaItem *raw = item.get();
    set.addItem(std::move(item));
    return raw;
}

// The line beginning with this label, or empty if there is none.
std::string lineWith(const std::vector<std::string> &lines, const std::string &label) {
    for (const std::string &line : lines) {
        if (line.rfind(label, 0) == 0) {
            return line;
        }
    }
    return std::string();
}

}  // namespace

TEST(one_item_gives_its_title_type_date_and_album) {
    MediaSet set;
    set.mName = "Holiday";
    MediaItem *item = addItem(set, "On the beach", "image/jpeg", Dates::startOfYearMs(1998), MediaItem::PRECISION_DAY);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.mediaItems.push_back(item);
    bucket.hasItems = true;
    selection.get().push_back(bucket);

    const std::vector<std::string> lines = MediaDetails::linesFor(selection);
    CHECK_EQ(lines.size(), (size_t)5);
    CHECK(lineWith(lines, "Title:") == "Title: On the beach");
    // The extension in capitals, not the mime type: "image/jpeg" says JPEG and
    // nothing a reader needs beyond it.
    CHECK(lineWith(lines, "Type:") == "Type: JPEG");
    CHECK(lineWith(lines, "Album:") == "Album: Holiday");
    CHECK(!lineWith(lines, "Taken on:").empty());
    CHECK(!lineWith(lines, "Location:").empty());
}

TEST(a_date_is_said_only_as_precisely_as_it_is_known) {
    // A catalogue that recorded a year is stored as the first of January.
    // Printing "01 Jan" would invent a day nobody wrote down.
    MediaSet set;
    set.mName = "Prints and Drawings";
    MediaItem *item = addItem(set, "Antiphonary", "image/jpeg", Dates::startOfYearMs(1465), MediaItem::PRECISION_YEAR);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.mediaItems.push_back(item);
    bucket.hasItems = true;
    selection.get().push_back(bucket);

    CHECK(lineWith(MediaDetails::linesFor(selection), "Taken on:") == "Taken on: 1465");
}

TEST(a_date_before_the_year_one_is_said_as_bc) {
    MediaSet set;
    set.mName = "Arts of Africa";
    // ISO year -888 is 889 BC: ISO counts 1 BC as year zero.
    MediaItem *item = addItem(set, "Coffin", "image/jpeg", Dates::startOfYearMs(-888), MediaItem::PRECISION_YEAR);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.mediaItems.push_back(item);
    bucket.hasItems = true;
    selection.get().push_back(bucket);

    CHECK(lineWith(MediaDetails::linesFor(selection), "Taken on:") == "Taken on: 889 BC");
}

TEST(an_undated_item_says_so_rather_than_guessing) {
    MediaSet set;
    set.mName = "Scans";
    MediaItem *item = addItem(set, "Untitled", "image/png", 0, MediaItem::PRECISION_DAY);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.mediaItems.push_back(item);
    bucket.hasItems = true;
    selection.get().push_back(bucket);

    CHECK(lineWith(MediaDetails::linesFor(selection), "Taken on:") == "Taken on: Date unknown");
}

TEST(one_whole_album_counts_itself_and_gives_its_span) {
    MediaSet set;
    set.mName = "Holiday";
    addItem(set, "First", "image/jpeg", Dates::startOfYearMs(1998), MediaItem::PRECISION_YEAR);
    addItem(set, "Second", "image/jpeg", Dates::startOfYearMs(2004), MediaItem::PRECISION_YEAR);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.hasItems = false;
    selection.get().push_back(bucket);

    const std::vector<std::string> lines = MediaDetails::linesFor(selection);
    CHECK(lineWith(lines, "1 album") == "1 album selected");
    CHECK(lineWith(lines, "2 items") == "2 items selected");
    CHECK(lineWith(lines, "Start:") == "Start: 1998");
    CHECK(lineWith(lines, "End:") == "End: 2004");
}

TEST(several_albums_count_the_albums_and_all_their_items) {
    MediaSet first;
    first.mName = "One";
    addItem(first, "a", "image/jpeg", Dates::startOfYearMs(1990), MediaItem::PRECISION_YEAR);
    addItem(first, "b", "image/jpeg", Dates::startOfYearMs(1991), MediaItem::PRECISION_YEAR);
    MediaSet second;
    second.mName = "Two";
    addItem(second, "c", "image/jpeg", Dates::startOfYearMs(2010), MediaItem::PRECISION_YEAR);

    MediaBucketList selection;
    MediaBucket firstBucket;
    firstBucket.mediaSet = &first;
    firstBucket.hasItems = false;
    MediaBucket secondBucket;
    secondBucket.mediaSet = &second;
    secondBucket.hasItems = false;
    selection.get().push_back(firstBucket);
    selection.get().push_back(secondBucket);

    const std::vector<std::string> lines = MediaDetails::linesFor(selection);
    CHECK(lineWith(lines, "2 albums") == "2 albums selected");
    CHECK(lineWith(lines, "3 items") == "3 items selected");
    // The span runs across both, not just the first.
    CHECK(lineWith(lines, "Start:") == "Start: 1990");
    CHECK(lineWith(lines, "End:") == "End: 2010");
}

TEST(several_items_are_counted_rather_than_described_one_by_one) {
    MediaSet set;
    set.mName = "Holiday";
    MediaItem *first = addItem(set, "First", "image/jpeg", Dates::startOfYearMs(2001), MediaItem::PRECISION_YEAR);
    MediaItem *second = addItem(set, "Second", "image/jpeg", Dates::startOfYearMs(2003), MediaItem::PRECISION_YEAR);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.mediaItems.push_back(first);
    bucket.mediaItems.push_back(second);
    bucket.hasItems = true;
    selection.get().push_back(bucket);

    const std::vector<std::string> lines = MediaDetails::linesFor(selection);
    CHECK(lineWith(lines, "2 items") == "2 items selected");
    CHECK(lineWith(lines, "Title:").empty());
    CHECK(lineWith(lines, "Start:") == "Start: 2001");
}

TEST(a_span_is_only_as_precise_as_its_vaguest_member) {
    // One photograph taken to the second and one artwork known to the year.
    // Saying a day for the pair would claim the catalogue recorded one.
    MediaSet set;
    set.mName = "Mixed";
    addItem(set, "Photo", "image/jpeg", Dates::startOfYearMs(1999) + 86400000LL, MediaItem::PRECISION_DAY);
    addItem(set, "Artwork", "image/jpeg", Dates::startOfYearMs(1600), MediaItem::PRECISION_YEAR);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.hasItems = false;
    selection.get().push_back(bucket);

    const std::vector<std::string> lines = MediaDetails::linesFor(selection);
    CHECK(lineWith(lines, "Start:") == "Start: 1600");
    CHECK(lineWith(lines, "End:") == "End: 1999");
}

TEST(an_album_with_no_dates_says_so_on_both_ends) {
    MediaSet set;
    set.mName = "Scans";
    addItem(set, "a", "image/png", 0, MediaItem::PRECISION_DAY);

    MediaBucketList selection;
    MediaBucket bucket;
    bucket.mediaSet = &set;
    bucket.hasItems = false;
    selection.get().push_back(bucket);

    const std::vector<std::string> lines = MediaDetails::linesFor(selection);
    CHECK(lineWith(lines, "Start:") == "Start: Date unknown");
    CHECK(lineWith(lines, "End:") == "End: Date unknown");
}

TEST(nothing_selected_gives_no_lines) {
    MediaBucketList selection;
    CHECK_EQ(MediaDetails::linesFor(selection).size(), (size_t)0);
}
