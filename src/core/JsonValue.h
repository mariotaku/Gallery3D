// JSON field access with defaults for missing or null values. nlohmann::value()
// throws for null.
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
