// A test runner small enough not to need a framework.
//
// What is worth testing here is the arithmetic that has no picture attached:
// EXIF decoding, power of two padding, cover cropping, density buckets, nine
// patch guides, clustering. Every one of those has been wrong at some point in
// this port and none of them announced it. Anything that needs a GL context
// stays with the screenshot checks, which is where a rendering mistake shows up
// anyway.
#include "test_runner.h"

#include <SDL3/SDL.h>

#include <vector>

#include "app/App.h"
#include "graphics/Canvas.h"
#include "tests.h"

namespace {

std::vector<TestCase> &registry() {
    static std::vector<TestCase> cases;
    return cases;
}

int sFailures = 0;
bool sSkipped = false;
std::function<void(const std::string &)> sPrint;
std::string sFixtureRoot;

void print(const std::string &line) {
    if (sPrint) {
        sPrint(line);
    }
}

std::string plural(int count, const char *word) {
    return std::to_string(count) + " " + word + (count == 1 ? "" : "s");
}

}  // namespace

void registerTest(const char *name, void (*fn)()) {
    registry().push_back(TestCase{name, fn});
}

void reportFailure(const char *file, int line, const char *expression, const std::string &detail) {
    ++sFailures;
    print(std::string("    FAIL ") + file + ":" + std::to_string(line));
    print(std::string("      ") + expression);
    if (!detail.empty()) {
        print("      " + detail);
    }
}

void reportSkip(const std::string &reason) {
    sSkipped = true;
    print("    SKIP " + reason);
}

void reportNote(const std::string &text) {
    print("    NOTE " + text);
}

const std::string &fixtureRoot() {
    return sFixtureRoot;
}

std::vector<std::string> TestRunner::names() {
    std::vector<std::string> names;
    for (const TestCase &test : registry()) {
        names.push_back(test.name);
    }
    return names;
}

int TestRunner::run(const Options &options, Summary *summary) {
    sPrint = options.print;
    sFixtureRoot = options.fixtureRoot;
    sFailures = 0;
    if (summary != nullptr) {
        *summary = Summary();
    }

    // Bitmap and Canvas read files and build surfaces through SDL, so it has to
    // be up even though nothing here opens a window.
    if (!SDL_Init(0)) {
        print(std::string("SDL_Init failed: ") + SDL_GetError());
        return -1;
    }

    // The widgets compose text, so the fonts have to be open and the art has to
    // be findable. Density 1 keeps the expected sizes easy to reason about.
    App::ASSET_ROOT = options.assetRoot;
    App::PIXEL_DENSITY = 1.0f;
    App::UI_DENSITY = 1.0f;
    if (!Canvas::initFonts()) {
        // Every test that composes text would fail for this one reason, or
        // pass without checking anything.
        print("Canvas::initFonts failed, so no test ran");
        SDL_Quit();
        return -1;
    }

    int run = 0;
    int skipped = 0;
    for (const TestCase &test : registry()) {
        const std::string name(test.name);
        const bool wanted = !options.name.empty()
                                ? name == options.name
                                : options.filter.empty() || name.find(options.filter) != std::string::npos;
        if (!wanted) {
            continue;
        }
        ++run;
        sSkipped = false;
        const int failuresBefore = sFailures;
        print(std::string("  ") + test.name);
        test.fn();
        if (sSkipped && sFailures == failuresBefore) {
            ++skipped;
        }
    }

    print("");
    print(plural(run, "test") + ", " + plural(sFailures, "failure") + ", " + std::to_string(skipped) +
          " skipped");
    Canvas::shutdownFonts();
    SDL_Quit();
    if (summary != nullptr) {
        summary->tests = run;
        summary->failures = sFailures;
        summary->skipped = skipped;
    }
    return sFailures;
}
