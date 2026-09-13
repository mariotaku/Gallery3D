// Reading settings out of an ini file and the environment.
//
// Mostly about the order the two beat each other in, and about a key nobody
// recognises being reported: an unreported typo behaves exactly like a setting
// that does nothing.
#include "tests.h"

#include <map>
#include <string>

#include "app/Settings.h"

namespace {

// Stands in for the environment, so the test does not have to set real ones.
struct FakeEnvironment {
    std::map<std::string, std::string> variables;

    std::function<const char *(const char *)> lookup() const {
        return [this](const char *name) -> const char * {
            auto found = variables.find(name);
            return (found == variables.end()) ? nullptr : found->second.c_str();
        };
    }
};

}  // namespace

TEST(an_ini_file_reads_section_and_key_as_one_name) {
    Settings::Store store;
    store.readIni(
        "# a comment\n"
        "[backdrop]\n"
        "blur = box\n"
        "sigma = 4.5\n"
        "\n"
        "[wall]\n"
        "scale = 2.0\n",
        "test.ini");
    CHECK_EQ(store.complaints().size(), (size_t)0);
    CHECK(store.get("backdrop.blur", "") == "box");
    CHECK_NEAR(store.getFloat("backdrop.sigma", 0.0f), 4.5, 0.001);
    CHECK_NEAR(store.getFloat("wall.scale", 0.0f), 2.0, 0.001);
}

TEST(whitespace_and_comment_markers_are_forgiven) {
    Settings::Store store;
    store.readIni(
        "   ; semicolons too\n"
        "  [ Backdrop ]  \n"
        "   BLUR   =   gaussian   \n",
        "test.ini");
    CHECK_EQ(store.complaints().size(), (size_t)0);
    CHECK(store.get("backdrop.blur", "") == "gaussian");
}

TEST(an_unknown_key_is_reported_rather_than_dropped) {
    Settings::Store store;
    store.readIni(
        "[backdrop]\n"
        "sigmma = 4.0\n",
        "test.ini");
    CHECK_EQ(store.complaints().size(), (size_t)1);
    CHECK(!store.has("backdrop.sigmma"));
    // And it names the file and line, because that is what makes it fixable.
    CHECK(store.complaints()[0].find("test.ini:2") != std::string::npos);
}

TEST(a_key_outside_a_section_is_reported) {
    Settings::Store store;
    store.readIni("sigma = 4.0\n", "test.ini");
    CHECK_EQ(store.complaints().size(), (size_t)1);
    CHECK(!store.has("backdrop.sigma"));
}

TEST(a_blank_value_leaves_the_default_alone) {
    // So a file can list every setting with the empty ones acting as its own
    // documentation, rather than forcing a value on everything it mentions.
    Settings::Store store;
    store.readIni(
        "[backdrop]\n"
        "blur =\n"
        "sigma = 3\n",
        "test.ini");
    CHECK_EQ(store.complaints().size(), (size_t)0);
    CHECK(!store.has("backdrop.blur"));
    CHECK(store.has("backdrop.sigma"));
}

TEST(the_environment_beats_the_file) {
    Settings::Store store;
    store.readIni(
        "[backdrop]\n"
        "blur = box\n"
        "sigma = 4.5\n",
        "test.ini");
    FakeEnvironment environment;
    environment.variables["GALLERY3D_BACKDROP_BLUR"] = "gaussian";
    store.readEnvironment(environment.lookup());

    CHECK(store.get("backdrop.blur", "") == "gaussian");
    CHECK(store.sourceOf("backdrop.blur") == "environment");
    // And it leaves alone what it did not mention.
    CHECK_NEAR(store.getFloat("backdrop.sigma", 0.0f), 4.5, 0.001);
    CHECK(store.sourceOf("backdrop.sigma") == "test.ini");
}

TEST(an_empty_environment_variable_does_not_count_as_set) {
    // An exported but empty variable is how a shell says "unset this", not
    // "set it to nothing", and taking it literally would blank a setting the
    // file had made.
    Settings::Store store;
    store.readIni(
        "[backdrop]\n"
        "blur = box\n",
        "test.ini");
    FakeEnvironment environment;
    environment.variables["GALLERY3D_BACKDROP_BLUR"] = "";
    store.readEnvironment(environment.lookup());
    CHECK(store.get("backdrop.blur", "") == "box");
}

TEST(the_environment_name_follows_from_the_setting_name) {
    // Derived rather than listed, so the file, the environment and the help
    // cannot disagree about what a setting is called.
    CHECK(Settings::environmentNameFor("backdrop.sigma") == "GALLERY3D_BACKDROP_SIGMA");
    CHECK(Settings::environmentNameFor("window.safe-area") == "GALLERY3D_WINDOW_SAFE_AREA");
    for (const Settings::Known &setting : Settings::known()) {
        const std::string variable = Settings::environmentNameFor(setting.name);
        CHECK(variable.rfind("GALLERY3D_", 0) == 0);
        CHECK(variable.find('.') == std::string::npos);
        CHECK(variable.find('-') == std::string::npos);
    }
}

TEST(booleans_take_the_spellings_people_actually_write) {
    Settings::Store store;
    store.readIni(
        "[library]\n"
        "artic = yes\n",
        "test.ini");
    CHECK(store.getBool("library.artic", false));

    Settings::Store off;
    off.readIni(
        "[library]\n"
        "artic = off\n",
        "test.ini");
    CHECK(!off.getBool("library.artic", true));

    // Anything that is not a yes or a no keeps the default rather than
    // guessing, so a value like "maybe" cannot quietly mean false.
    Settings::Store nonsense;
    nonsense.readIni(
        "[library]\n"
        "artic = maybe\n",
        "test.ini");
    CHECK(nonsense.getBool("library.artic", true));
    CHECK(!nonsense.getBool("library.artic", false));
}

TEST(a_malformed_line_is_reported_and_the_rest_still_reads) {
    // One bad line does not throw away the file around it.
    Settings::Store store;
    store.readIni(
        "[backdrop]\n"
        "this line has no equals sign\n"
        "sigma = 5\n",
        "test.ini");
    CHECK_EQ(store.complaints().size(), (size_t)1);
    CHECK_NEAR(store.getFloat("backdrop.sigma", 0.0f), 5.0, 0.001);
}

TEST(every_setting_is_a_section_and_a_key_with_something_to_say) {
    // The table is what the help prints from, so a malformed entry is a
    // malformed help page.
    for (const Settings::Known &setting : Settings::known()) {
        CHECK(setting.name != nullptr && setting.name[0] != '\0');
        CHECK(setting.summary != nullptr && setting.summary[0] != '\0');
        CHECK(std::string(setting.name).find('.') != std::string::npos);
    }
}
