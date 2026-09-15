// Runs the registered tests. It is kept apart from main so a host other than
// the desktop console, such as an Android app, runs the same tests.
#pragma once

#include <functional>
#include <string>

namespace TestRunner {

struct Options {
    // Runs only the tests whose name contains this. Empty runs every test.
    std::string filter;
    // The folder that holds the app's assets: fonts and drawables.
    std::string assetRoot;
    // The folder tests/fixtures is at.
    std::string fixtureRoot;
    // Takes each line of the report, without its line break.
    std::function<void(const std::string &line)> print;
};

// Runs the tests and returns the number of failed checks, or -1 when the
// environment the tests need could not be set up.
int run(const Options &options);

}  // namespace TestRunner
