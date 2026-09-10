// A test runner small enough not to need a framework.
//
// What is worth testing here is the arithmetic that has no picture attached:
// EXIF decoding, power of two padding, cover cropping, density buckets, nine
// patch guides, clustering. Every one of those has been wrong at some point in
// this port and none of them announced it. Anything that needs a GL context
// stays with the screenshot checks, which is where a rendering mistake shows up
// anyway.
#include "tests.h"

#include <cstdio>

#include <SDL3/SDL.h>

namespace {

std::vector<TestCase> &registry() {
    static std::vector<TestCase> cases;
    return cases;
}

int sFailures = 0;
const char *sCurrentTest = "";

}  // namespace

void registerTest(const char *name, void (*fn)()) {
    registry().push_back(TestCase{name, fn});
}

void reportFailure(const char *file, int line, const char *expression, const std::string &detail) {
    ++sFailures;
    std::printf("    FAIL %s:%d\n      %s\n", file, line, expression);
    if (!detail.empty()) {
        std::printf("      %s\n", detail.c_str());
    }
}

int main() {
    // Bitmap decodes through SDL_image and Canvas measures through SDL_ttf, so
    // both have to be up even though nothing here opens a window.
    if (!SDL_Init(0)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int failedCases = 0;
    for (const TestCase &test : registry()) {
        sCurrentTest = test.name;
        int before = sFailures;
        std::printf("  %s\n", test.name);
        test.fn();
        if (sFailures != before) {
            ++failedCases;
        }
    }

    std::printf("\n%d test%s, %d failure%s\n", (int)registry().size(), registry().size() == 1 ? "" : "s",
                sFailures, sFailures == 1 ? "" : "s");
    SDL_Quit();
    return (sFailures == 0) ? 0 : 1;
}
