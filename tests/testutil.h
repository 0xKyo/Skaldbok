// Tiny helpers shared by the test programs.
#pragma once

#include <cstdio>
#include <string>

#include <SDL3/SDL.h>

namespace test {

inline int failures = 0;

inline void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (!ok) ++failures;
}

inline int finish() {
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}

inline std::string dataDir(int argc, char** argv) { return argc > 1 ? argv[1] : SKALDBOK_DEV_DATA_DIR; }

inline std::string sourceDir() { return SKALDBOK_SOURCE_DIR; }

// A clean scratch folder under the build tree (forward slashes, no trailing slash).
inline std::string scratch(const std::string& name) {
    std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "./";
    for (char& c : base)
        if (c == '\\') c = '/';
    const std::string dir = base + "scratch-" + name;
    SDL_CreateDirectory(dir.c_str());
    return dir;
}

inline bool write(const std::string& path, const std::string& text) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) SDL_CreateDirectory(path.substr(0, slash).c_str());
    return SDL_SaveFile(path.c_str(), text.data(), text.size());
}

inline bool exists(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info);
}

}  // namespace test
