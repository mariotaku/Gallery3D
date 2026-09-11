// Turning one of the museum's artwork records into an item.
//
// The fields are the api's, so the checks are against records shaped like the
// ones it actually returns.
#include "tests.h"

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "ArticDataSource.h"
#include "DateLabels.h"
#include "MediaItem.h"

namespace {

nlohmann::json artwork(const char *title, int64_t dateEnd) {
    nlohmann::json record;
    record["id"] = 12345;
    record["title"] = title;
    record["image_id"] = "2d484387-2509-5e8e-2c43-22f9981972eb";
    record["date_end"] = dateEnd;
    record["thumbnail"]["width"] = 9310;
    record["thumbnail"]["height"] = 6237;
    return record;
}

std::string yearShown(const nlohmann::json &record) {
    ArticDataSource source;
    std::unique_ptr<MediaItem> item = source.makeItem(record);
    if (!item || !item->isDateTakenValid()) {
        return std::string();
    }
    return DateLabels::atPrecision(item->mDateTakenInMs, item->mDatePrecision);
}

}  // namespace

TEST(a_bc_year_keeps_the_number_the_museum_prints) {
    // The api counts BC without a year zero: date_end -889 is the "about
    // 924-889 BCE" on the coffin's own label. ISO has a year zero, so reading
    // -889 as an ISO year lands a year early.
    CHECK(yearShown(artwork("Coffin of Ipi-ha-ishutef", -889)) == "889 BC");
    CHECK(yearShown(artwork("Stela of Amenemhat", -1877)) == "1877 BC");
    CHECK(yearShown(artwork("Bronze bell", -771)) == "771 BC");
}

TEST(an_ad_year_is_itself) {
    CHECK(yearShown(artwork("A Sunday on La Grande Jatte", 1884)) == "1884");
    CHECK(yearShown(artwork("Antiphonary", 1465)) == "1465");
}

TEST(a_year_the_museum_does_not_know_leaves_the_item_undated) {
    // A missing field and a zero read the same, and the api gives no year
    // rather than the year 1 BC when it does not know.
    CHECK(yearShown(artwork("Untitled", 0)).empty());

    nlohmann::json missing = artwork("Untitled", 0);
    missing.erase("date_end");
    CHECK(yearShown(missing).empty());
}

TEST(the_original_size_comes_from_the_thumbnail_field) {
    // Named for the thumbnail but measuring the original, which is what the
    // tiled view lays its grid over.
    ArticDataSource source;
    std::unique_ptr<MediaItem> item = source.makeItem(artwork("A Sunday on La Grande Jatte", 1884));
    CHECK(item != nullptr);
    if (item) {
        CHECK_EQ(item->mFullWidth, 9310);
        CHECK_EQ(item->mFullHeight, 6237);
        CHECK(item->hasFullSize());
    }
}

TEST(a_record_with_no_picture_makes_no_item) {
    nlohmann::json record = artwork("Untitled", 1900);
    record.erase("image_id");
    ArticDataSource source;
    CHECK(source.makeItem(record) == nullptr);
}
