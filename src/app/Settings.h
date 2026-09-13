// Settings precedence: compiled defaults < INI file < environment; no setting flags.
// Names use section.key: backdrop.sigma maps to [backdrop] sigma
// and GALLERY3D_BACKDROP_SIGMA.
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
    const char *summary;  // one line, for the usage text
};

const std::vector<Known> &known();

// The environment variable a setting is read from: GALLERY3D_BACKDROP_SIGMA.
std::string environmentNameFor(const std::string &name);

// What was actually set, and where each value came from.
class Store {
  public:
    // Read the file, then environment overrides. explicitPath comes from --config or
    // GALLERY3D_CONFIG; empty searches defaults. Return false if an explicit file is
    // unreadable.
    bool load(const std::string &explicitPath);

    // File and environment parsing entry points.
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

    // Warnings for unknown or unsectioned keys. The caller must print them.
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
