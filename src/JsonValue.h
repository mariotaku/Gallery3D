// Reading fields out of json that may not be there, and may be there as null.
//
// nlohmann's value() covers a missing key and nothing else. A key that is
// present and null throws type_error, and api.artic.edu sets date_end,
// artist_title and plenty else to null whenever it has no answer. One null in
// one record took the whole loader thread down, which is why these exist.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

inline std::string stringOr(const nlohmann::json &value, const char *key, const char *fallback) {
    auto found = value.find(key);
    if (found == value.end() || !found->is_string()) {
        return fallback;
    }
    return found->get<std::string>();
}

inline int64_t intOr(const nlohmann::json &value, const char *key, int64_t fallback) {
    auto found = value.find(key);
    if (found == value.end() || !found->is_number_integer()) {
        return fallback;
    }
    return found->get<int64_t>();
}
