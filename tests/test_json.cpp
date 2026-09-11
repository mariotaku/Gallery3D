// Reading fields that a real api actually sends.
//
// These are here because of a crash. api.artic.edu answers date_end with null
// on records it has no year for, nlohmann's value() throws on that rather than
// returning the fallback, and the exception took the loader thread with it. The
// wall came up empty and said nothing about why.
#include "tests.h"

#include "JsonValue.h"

TEST(reading_a_field_that_is_not_there_gives_the_fallback) {
    nlohmann::json empty = nlohmann::json::object();
    CHECK_EQ(intOr(empty, "date_end", -1), (int64_t)-1);
    CHECK(stringOr(empty, "title", "Untitled") == std::string("Untitled"));
}

TEST(reading_a_field_that_is_null_gives_the_fallback) {
    // The one that bit. Present, and null, which is not the same as absent.
    nlohmann::json record = {{"date_end", nullptr}, {"artist_title", nullptr}};
    CHECK_EQ(intOr(record, "date_end", -1), (int64_t)-1);
    CHECK(stringOr(record, "artist_title", "") == std::string(""));
}

TEST(reading_a_field_of_the_wrong_type_gives_the_fallback) {
    // Nothing promises an api keeps its types, and a wrong one throws just the
    // same as a null.
    nlohmann::json record = {{"date_end", "1889"}, {"title", 42}};
    CHECK_EQ(intOr(record, "date_end", -1), (int64_t)-1);
    CHECK(stringOr(record, "title", "Untitled") == std::string("Untitled"));
}

TEST(reading_a_field_that_is_there_gives_the_value) {
    nlohmann::json record = {{"id", 27992}, {"title", "The Bedroom"}, {"date_end", 1889}};
    CHECK_EQ(intOr(record, "id", 0), (int64_t)27992);
    CHECK_EQ(intOr(record, "date_end", 0), (int64_t)1889);
    CHECK(stringOr(record, "title", "Untitled") == std::string("The Bedroom"));
}
