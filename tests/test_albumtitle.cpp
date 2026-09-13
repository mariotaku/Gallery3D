// The string the album label is built from.
//
// MediaSet keeps two titles: the whole one, and one cut to sixteen characters
// with an ellipsis put in the middle. The label box measures its string and
// ellipsizes what will not fit, so handing it the already-cut one truncates
// twice and ends the label on six dots.
#include "tests.h"

#include <map>
#include <memory>
#include <string>

#include "DisplaySlot.h"
#include "MediaSet.h"
#include "Texture.h"

namespace {

// A set named and counted the way a data source leaves one.
MediaSet namedSet(const std::string &name, int count) {
    MediaSet set;
    set.mName = name;
    set.setNumExpectedItems(count);
    set.generateTitle(true);
    return set;
}

// The string a slot asked for a texture of. getTitleImage caches by text, so
// the key is what the label was built from.
std::string titleAskedFor(MediaSet &set) {
    DisplaySlot slot;
    slot.setMediaSet(&set);
    std::map<std::string, std::shared_ptr<StringTexture>> table;
    slot.getTitleImage(table);
    return table.empty() ? std::string() : table.begin()->first;
}

int countOf(const std::string &text, const std::string &part) {
    int found = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.length())) {
        ++found;
    }
    return found;
}

}  // namespace

TEST(a_long_album_name_is_handed_over_whole) {
    MediaSet set = namedSet("FINAL FANTASY XIV", 12);

    // MediaSet still cuts its own copy, and that copy carries an ellipsis.
    CHECK(countOf(set.mTruncTitleString, "...") == 1);

    // The label gets the uncut one, so the box does the only cutting.
    CHECK(countOf(titleAskedFor(set), "...") == 0);
}

TEST(the_label_never_ends_up_with_two_ellipses) {
    // Names past sixteen characters are where the two cuts used to meet.
    const char *names[] = {"FINAL FANTASY XIV", "C:\\Users\\Mariotaku\\Pictures", "Hokkaido Autumn Trip 2026",
                           "a_very_long_album_name_indeed"};
    for (const char *name : names) {
        MediaSet set = namedSet(name, 4);
        CHECK(countOf(titleAskedFor(set), "...") == 0);
    }
}

TEST(a_short_album_name_keeps_its_count) {
    MediaSet set = namedSet("Kyoto", 8);

    // Nothing to cut, so the label carries the name and the count as they are.
    // CHECK_EQ has no string overload, so compare and report by hand.
    const std::string asked = titleAskedFor(set);
    CHECK(asked == "Kyoto  (8)");
}

TEST(a_name_of_exactly_sixteen_is_not_cut) {
    // The boundary MediaSet::generateTitle tests with >, so sixteen stays whole.
    MediaSet set = namedSet("sixteen_chars_16", 1);
    CHECK(countOf(set.mTruncTitleString, "...") == 0);
    CHECK(countOf(titleAskedFor(set), "...") == 0);
}
