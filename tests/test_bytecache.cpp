// The bounded store behind a network data source.
//
// The point of these is the bound. The unbounded version works perfectly right
// up until the session has been running a while, which is exactly the kind of
// fault that never shows up in a screenshot check.
#include "tests.h"

#include <string>
#include <thread>
#include <vector>

#include "core/ByteCache.h"

namespace {

std::vector<uint8_t> blob(size_t size, uint8_t fill) {
    return std::vector<uint8_t>(size, fill);
}

}  // namespace

TEST(a_stored_blob_comes_back_unchanged) {
    ByteCache cache(1024);
    cache.put("a", blob(10, 0x7F));

    std::vector<uint8_t> out;
    CHECK(cache.get("a", &out));
    CHECK_EQ(out.size(), (size_t)10);
    CHECK_EQ((int)out[0], 0x7F);
    CHECK_EQ(cache.bytes(), (size_t)10);
}

TEST(a_miss_leaves_the_output_alone) {
    ByteCache cache(1024);
    std::vector<uint8_t> out = blob(3, 0x11);
    CHECK(!cache.get("nothing", &out));
    // Untouched, so a caller can tell a miss from an empty entry.
    CHECK_EQ(out.size(), (size_t)3);
}

TEST(the_store_never_grows_past_its_budget) {
    // The whole reason this class exists. Twenty blobs into a budget that fits
    // four.
    ByteCache cache(400);
    for (int i = 0; i < 20; ++i) {
        cache.put("k" + std::to_string(i), blob(100, (uint8_t)i));
    }
    CHECK(cache.bytes() <= (size_t)400);
    CHECK_EQ(cache.count(), (size_t)4);
}

TEST(the_least_recently_used_entry_goes_first) {
    ByteCache cache(300);
    cache.put("a", blob(100, 1));
    cache.put("b", blob(100, 2));
    cache.put("c", blob(100, 3));

    // Touching "a" makes "b" the oldest, so the next insert should take "b".
    std::vector<uint8_t> out;
    CHECK(cache.get("a", &out));

    cache.put("d", blob(100, 4));
    CHECK(cache.get("a", &out));
    CHECK(!cache.get("b", &out));
    CHECK(cache.get("c", &out));
    CHECK(cache.get("d", &out));
}

TEST(storing_the_same_key_twice_replaces_rather_than_doubles) {
    ByteCache cache(1024);
    cache.put("a", blob(100, 1));
    cache.put("a", blob(40, 2));

    CHECK_EQ(cache.count(), (size_t)1);
    // Without dropping the old size first this reads 140 and the accounting
    // drifts further every time an entry is rewritten.
    CHECK_EQ(cache.bytes(), (size_t)40);

    std::vector<uint8_t> out;
    CHECK(cache.get("a", &out));
    CHECK_EQ(out.size(), (size_t)40);
    CHECK_EQ((int)out[0], 2);
}

TEST(a_blob_too_big_for_the_budget_is_not_stored) {
    // Keeping it would mean evicting everything else to hold one entry, which
    // is worse than not caching it at all.
    ByteCache cache(100);
    cache.put("small", blob(50, 1));
    cache.put("huge", blob(500, 2));

    std::vector<uint8_t> out;
    CHECK(!cache.get("huge", &out));
    CHECK(cache.get("small", &out));
    CHECK_EQ(cache.bytes(), (size_t)50);
}

TEST(an_empty_blob_is_not_stored) {
    // A failed download should not read back as a hit on the next attempt.
    ByteCache cache(1024);
    cache.put("a", {});
    std::vector<uint8_t> out;
    CHECK(!cache.get("a", &out));
}

TEST(several_threads_can_use_it_at_once) {
    // The texture threads all read through this. A race here would show up as
    // a rare crash a long way from the cause.
    ByteCache cache(64 * 1024);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < 200; ++i) {
                std::string key = "k" + std::to_string((t * 200 + i) % 50);
                std::vector<uint8_t> out;
                if (!cache.get(key, &out)) {
                    cache.put(key, blob(128, (uint8_t)i));
                }
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }
    CHECK(cache.bytes() <= (size_t)(64 * 1024));
    CHECK(cache.count() <= (size_t)50);
}

TEST(clearing_empties_it) {
    ByteCache cache(1024);
    cache.put("a", blob(100, 1));
    cache.clear();
    CHECK_EQ(cache.count(), (size_t)0);
    CHECK_EQ(cache.bytes(), (size_t)0);
}
