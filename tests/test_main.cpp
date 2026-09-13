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

#include "app/App.h"
#include "core/Backtrace.h"
#include "graphics/Canvas.h"

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
    // A test that crashes reports an exit code and nothing else, which says
    // less than the assertion it was about to make.
    Backtrace::install();

    // Bitmap and Canvas read files and build surfaces through SDL, so it has to
    // be up even though nothing here opens a window.
    if (!SDL_Init(0)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // The widgets compose text, so the fonts have to be open and the art has to
    // be findable. Density 1 keeps the expected sizes easy to reason about.
    App::ASSET_ROOT = GALLERY3D_ASSET_ROOT;
    App::PIXEL_DENSITY = 1.0f;
    App::UI_DENSITY = 1.0f;
    if (!Canvas::initFonts()) {
        std::printf("Canvas::initFonts failed, text will not compose\n");
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
    Canvas::shutdownFonts();
    SDL_Quit();
    return (sFailures == 0) ? 0 : 1;
}
