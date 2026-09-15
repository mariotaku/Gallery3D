// The desktop console host for the tests. With an argument, runs only the tests
// whose name contains it.
#include <cstdio>

#include "core/Backtrace.h"
#include "test_runner.h"

int main(int argc, char **argv) {
    // A test that crashes reports an exit code and nothing else, which says
    // less than the assertion it was about to make.
    Backtrace::install();

    TestRunner::Options options;
    options.filter = (argc > 1) ? argv[1] : "";
    options.assetRoot = GALLERY3D_ASSET_ROOT;
    options.fixtureRoot = GALLERY3D_FIXTURE_ROOT;
    options.print = [](const std::string &line) {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    };
    return (TestRunner::run(options) == 0) ? 0 : 1;
}
