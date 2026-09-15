// A number for a text key that is the same on every run, for the ids of sets
// and items whose source names them by a path or a uri.
#pragma once

#include <cstdint>
#include <string>

// FNV-1a over the bytes, kept non-negative: ids are compared against
// Shared::INVALID, which is -1. The arithmetic is unsigned, where an overflow
// wraps rather than being undefined.
inline int64_t stableIdFor(const std::string &key) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char character : key) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return (int64_t)(hash & 0x7FFFFFFFFFFFFFFFULL);
}
