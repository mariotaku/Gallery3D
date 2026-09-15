// Runs the registered tests. It is kept apart from main so a host other than
// the desktop console, such as an Android app, runs the same tests.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace TestRunner {

struct Options {
    // Runs only the tests whose name contains this. Empty runs every test.
    std::string filter;
    // Runs only the test with exactly this name, in place of filter, when not
    // empty.
    std::string name;
    // The folder that holds the app's assets: fonts and drawables.
    std::string assetRoot;
    // The folder tests/fixtures is at.
    std::string fixtureRoot;
    // Takes each line of the report, without its line break.
    std::function<void(const std::string &line)> print;
};

// What a run counted. All zero when the run could not start.
struct Summary {
    int tests = 0;
    int failures = 0;
    int skipped = 0;
};

// The registered tests' names, in the order a run takes them.
std::vector<std::string> names();

// Runs the tests and returns the number of failed checks, or -1 when the
// environment the tests need could not be set up. Fills summary when given.
int run(const Options &options, Summary *summary = nullptr);

}  // namespace TestRunner
