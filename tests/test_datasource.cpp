// What a source says it can do, and what the feed does with that answer.
//
// The point of these is the read only case. A source like the media store has
// no delete, and the HUD asks before it offers the button, so the answer has to
// be right before anything is drawn.
#include "tests.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "media/ConcatenatedDataSource.h"
#include "media/LocalDataSource.h"
#include "media/MediaBucketList.h"
#include "media/MediaFeed.h"
#include "media/MediaItem.h"
#include "media/MediaSet.h"
#include "graphics/Texture.h"

namespace {

// Stands in for a source like the Android media store: it can list and it can
// hand over bytes, and that is all.
class ReadOnlySource : public DataSource {
  public:
    void loadMediaSets(MediaFeed *feed) override {
        (void)feed;
    }
    void loadItemsForSet(MediaFeed *feed, MediaSet *parentSet) override {
        (void)feed;
        (void)parentSet;
    }
    bool readItemBytes(MediaItem *item, std::vector<uint8_t> *bytes) override {
        (void)item;
        // Whatever it hands back, the point is that it hands back bytes rather
        // than a path on this disk.
        bytes->assign(4, 0x7F);
        return true;
    }
};

// A stub that claims it can do everything, to check the feed is reading the
// answer rather than assuming one.
class WritableSource : public ReadOnlySource {
  public:
    bool supportsOperation(int operation) const override {
        (void)operation;
        return true;
    }
};

// One set holding one item, owned by the given source.
struct Fixture {
    explicit Fixture(DataSource *source) {
        set.mDataSource = source;
        auto owned = std::make_unique<MediaItem>();
        item = owned.get();
        item->mId = 1;
        item->mFilePath = "nowhere.jpg";
        set.addItem(std::move(owned));

        MediaBucket bucket;
        bucket.mediaSet = &set;
        bucket.mediaItems.push_back(item);
        bucket.hasItems = true;
        buckets.push_back(bucket);
    }
    MediaSet set;
    MediaItem *item = nullptr;
    std::vector<MediaBucket> buckets;
};

}  // namespace

TEST(a_read_only_source_reports_it_cannot_delete) {
    ReadOnlySource source;
    CHECK(!source.supportsOperation(MediaFeed::OPERATION_DELETE));
    CHECK(!source.supportsOperation(MediaFeed::OPERATION_ROTATE));
}

TEST(the_local_source_can_delete_and_rotate) {
    LocalDataSource source("nowhere");
    CHECK(source.supportsOperation(MediaFeed::OPERATION_DELETE));
    CHECK(source.supportsOperation(MediaFeed::OPERATION_ROTATE));
    // And nothing else, so a new operation has to opt in rather than inherit.
    CHECK(!source.supportsOperation(MediaFeed::OPERATION_CROP));
}

TEST(the_feed_refuses_a_selection_its_source_cannot_delete) {
    ReadOnlySource source;
    MediaFeed feed(&source, nullptr);
    Fixture fixture(&source);
    CHECK(!feed.selectionSupports(MediaFeed::OPERATION_DELETE, &fixture.buckets));
}

TEST(the_feed_allows_a_selection_its_source_can_delete) {
    WritableSource source;
    MediaFeed feed(&source, nullptr);
    Fixture fixture(&source);
    CHECK(feed.selectionSupports(MediaFeed::OPERATION_DELETE, &fixture.buckets));
}

TEST(one_undeletable_item_disqualifies_the_whole_selection) {
    // Two sources on one wall, only one of which can delete. A partial delete
    // is worse than none, so the answer for the pair is no.
    WritableSource writable;
    ReadOnlySource readOnly;
    MediaFeed feed(&writable, nullptr);

    Fixture good(&writable);
    Fixture bad(&readOnly);
    std::vector<MediaBucket> mixed = good.buckets;
    mixed.insert(mixed.end(), bad.buckets.begin(), bad.buckets.end());

    CHECK(feed.selectionSupports(MediaFeed::OPERATION_DELETE, &good.buckets));
    CHECK(!feed.selectionSupports(MediaFeed::OPERATION_DELETE, &mixed));
}

TEST(an_empty_selection_answers_for_the_feeds_own_source) {
    // Between entering selection mode and picking something there is nothing to
    // ask about, and the bar should not flash a button it is about to remove.
    std::vector<MediaBucket> empty;
    {
        ReadOnlySource source;
        MediaFeed feed(&source, nullptr);
        CHECK(!feed.selectionSupports(MediaFeed::OPERATION_DELETE, &empty));
    }
    {
        WritableSource source;
        MediaFeed feed(&source, nullptr);
        CHECK(feed.selectionSupports(MediaFeed::OPERATION_DELETE, &empty));
    }
}

TEST(concatenated_reports_what_either_side_can_do) {
    WritableSource writable;
    ReadOnlySource readOnly;
    ConcatenatedDataSource both(&readOnly, &writable);
    // Asked with no item in hand, so this is only "could anything here do it".
    CHECK(both.supportsOperation(MediaFeed::OPERATION_DELETE));

    ConcatenatedDataSource neither(&readOnly, &readOnly);
    CHECK(!neither.supportsOperation(MediaFeed::OPERATION_DELETE));
}

TEST(bytes_come_back_from_a_source_that_has_no_files) {
    ReadOnlySource source;
    MediaItem item;
    std::vector<uint8_t> bytes;
    CHECK(source.readItemBytes(&item, &bytes));
    CHECK(!bytes.empty());

    // The local source has files, so it declines and the reader falls back to
    // the path. That is what keeps it free of any copying.
    LocalDataSource local("nowhere");
    std::vector<uint8_t> unused;
    CHECK(!local.readItemBytes(&item, &unused));
}

TEST(decoding_from_memory_matches_decoding_from_the_file) {
    // The seam has to produce the same picture either way, or a remote source
    // would render differently from a local one.
    std::string path = fixtureRoot() + "/decode/opaque.png";
    Bitmap fromFile = Bitmap::load(path, 0);
    CHECK(fromFile.valid());

    std::FILE *file = std::fopen(path.c_str(), "rb");
    CHECK(file != nullptr);
    if (file == nullptr || !fromFile.valid()) {
        return;
    }
    std::vector<uint8_t> encoded;
    std::fseek(file, 0, SEEK_END);
    encoded.resize((size_t)std::ftell(file));
    std::fseek(file, 0, SEEK_SET);
    size_t read = std::fread(encoded.data(), 1, encoded.size(), file);
    std::fclose(file);
    CHECK_EQ(read, encoded.size());

    Bitmap fromMemory = Bitmap::loadFromMemory(encoded.data(), encoded.size(), 0);
    CHECK(fromMemory.valid());
    if (!fromMemory.valid()) {
        return;
    }
    CHECK_EQ(fromMemory.width(), fromFile.width());
    CHECK_EQ(fromMemory.height(), fromFile.height());
    bool identical = true;
    for (int i = 0; i < fromFile.width() * fromFile.height() * 4; ++i) {
        if (fromFile.pixels()[i] != fromMemory.pixels()[i]) {
            identical = false;
            break;
        }
    }
    CHECK(identical);
}

TEST(decoding_rubbish_from_memory_fails_rather_than_crashes) {
    // A truncated or failed download reaches the decoder eventually.
    const uint8_t rubbish[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    CHECK(!Bitmap::loadFromMemory(rubbish, sizeof(rubbish), 0).valid());
    CHECK(!Bitmap::loadFromMemory(nullptr, 0, 0).valid());
}

TEST(a_set_joins_the_feed_whole_and_only_between_frames) {
    // The draw reads sets without a lock while the loader thread builds them,
    // so a source hands a set over filled, and the feed takes it in from
    // pumpListener, which runs between frames.
    ReadOnlySource source;
    MediaFeed feed(&source, nullptr);

    auto set = std::make_unique<MediaSet>();
    set->mId = 7;
    auto first = std::make_unique<MediaItem>();
    first->mId = 1;
    set->addItem(std::move(first));
    MediaSet *handed = set.get();
    feed.addMediaSet(std::move(set));
    CHECK(feed.getMediaSets().empty());
    feed.pumpListener();
    CHECK(feed.getMediaSets().size() == 1 && feed.getMediaSets()[0] == handed);
    CHECK(handed->mDataSource == &source);

    // A later page waits for the next frame too, then gets its finishing step.
    std::vector<std::unique_ptr<MediaItem>> page;
    page.push_back(std::make_unique<MediaItem>());
    page.back()->mId = 2;
    int itemsWhenFinished = 0;
    feed.addItems(handed, std::move(page), [&itemsWhenFinished](MediaSet &filled) {
        itemsWhenFinished = filled.getNumItems();
    });
    CHECK_EQ(handed->getNumItems(), 1);
    feed.pumpListener();
    CHECK_EQ(handed->getNumItems(), 2);
    CHECK_EQ(itemsWhenFinished, 2);
    CHECK(handed->getItems()[1]->mParentMediaSet == handed);

    // A second set with the same id does not replace the one there, whose slot
    // and photos the wall may be holding, and a page for it still lands in the
    // set the feed kept.
    auto duplicate = std::make_unique<MediaSet>();
    duplicate->mId = 7;
    feed.addMediaSet(std::move(duplicate));
    std::vector<std::unique_ptr<MediaItem>> late;
    late.push_back(std::make_unique<MediaItem>());
    feed.addItems(handed, std::move(late));
    feed.pumpListener();
    CHECK(feed.getMediaSets().size() == 1 && feed.getMediaSets()[0] == handed);
    CHECK_EQ(handed->getNumItems(), 3);
}
