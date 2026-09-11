// Where the wall's settings come from.
//
// Four places, each beating the one before it:
//
//   the defaults compiled in
//   the ini file
//   the environment
//   the command line
//
// The file holds what you always want, the environment overrides it for one
// shell, a flag overrides it for one run.
//
// What belongs here is a setting - something that says how the wall should
// look. What stays on the command line alone is a verb: open this album,
// render that many frames, then quit. A verb describes one run and there is
// nothing to write down.
//
// One name per setting, written "section.key", and the rest follows from it.
// "backdrop.sigma" is `sigma` under `[backdrop]` in the file and
// GALLERY3D_BACKDROP_SIGMA in the environment. Flag spellings are their own,
// so that a flag that worked before still works.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Settings {

// Every setting that can be written down, and how to explain it. The one list:
// it validates the file, names the environment variables, and prints the help.
struct Known {
    const char *name;     // "backdrop.sigma"
    const char *flag;     // "--backdrop-sigma", or "" where there is none
    const char *summary;  // one line, for the usage text
};

const std::vector<Known> &known();

// The environment variable a setting is read from: GALLERY3D_BACKDROP_SIGMA.
std::string environmentNameFor(const std::string &name);

// What was actually set, and where each value came from.
class Store {
  public:
    // Reads the file, then lets the environment override it.
    //
    // `explicitPath` is --config or GALLERY3D_CONFIG; empty means look in the
    // usual places. Returns false only when a named file could not be read:
    // a path given explicitly and then ignored would leave the wall running on
    // settings nobody chose.
    bool load(const std::string &explicitPath);

    // The two halves, separately, so they can be tested without a filesystem or
    // an environment.
    void readIni(const std::string &text, const std::string &describedAs);
    void readEnvironment(const std::function<const char *(const char *)> &lookup);

    bool has(const std::string &name) const;
    std::string get(const std::string &name, const std::string &fallback) const;
    float getFloat(const std::string &name, float fallback) const;
    bool getBool(const std::string &name, bool fallback) const;

    // Where a value came from, for the log: the file's path, or "environment".
    std::string sourceOf(const std::string &name) const;

    // The file this ended up reading, empty if there was none.
    const std::string &path() const {
        return mPath;
    }

    // Lines that could not be used: an unknown key, or one outside any section.
    // Warnings rather than errors, so an old file still starts, but the caller
    // has to print them: an unreported typo is indistinguishable from a setting
    // that does nothing.
    const std::vector<std::string> &complaints() const {
        return mComplaints;
    }

  private:
    struct Entry {
        std::string value;
        std::string source;
    };
    std::map<std::string, Entry> mValues;
    std::vector<std::string> mComplaints;
    std::string mPath;
};

// The files to try, in order, when none was named: the working directory first
// so a checkout can carry its own, then the user's own config directory.
std::vector<std::string> searchPaths();

}  // namespace Settings
