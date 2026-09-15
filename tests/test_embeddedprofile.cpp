// The ICC profile a picture carries, found in every container that can carry
// one, and read the same way on every platform.
#include "tests.h"

#include <string>
#include <vector>

#include "graphics/Bitmap.h"
#include "graphics/EmbeddedProfile.h"

namespace {

std::vector<uint8_t> fixture(const std::string &name) {
    std::vector<uint8_t> bytes;
    Bitmap::readFile(fixtureRoot() + "/decode/" + name, &bytes);
    return bytes;
}

std::vector<uint8_t> profileIn(const std::string &name) {
    const std::vector<uint8_t> bytes = fixture(name);
    return EmbeddedProfile::of(bytes.data(), bytes.size());
}

}  // namespace

TEST(an_embedded_profile_is_found_in_every_container_that_carries_one) {
    const std::vector<uint8_t> expected = fixture("swapped.icc");
    CHECK(!expected.empty());
    for (const char *name : {"icc.jpg", "icc.png", "icc.webp", "icc.tif", "icc_translucent.png"}) {
        CHECK_DETAIL(profileIn(name) == expected, std::string(name) + ": the profile is missing or different");
    }
}

TEST(a_picture_without_a_profile_has_none) {
    for (const char *name : {"baseline.jpg", "opaque.png", "lossless.webp", "opaque.tif", "opaque.bmp",
                             "adobe_exif.jpg", "truncated.png", "rubbish.jpg", "empty.png"}) {
        CHECK_DETAIL(profileIn(name).empty(), std::string(name) + ": a profile was found");
    }
    CHECK(EmbeddedProfile::of(nullptr, 0).empty());
}
