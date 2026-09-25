#include "parsing/fsutil.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <random>

#include <SDL3/SDL.h>

namespace gm::fs {

std::optional<std::string> readFile(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return std::nullopt;
    std::string out(static_cast<const char*>(data), size);
    SDL_free(data);
    return out;
}

bool writeFile(const std::string& path, std::string_view data) {
    const std::string tmp = path + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), data.data(), data.size())) return false;
    // On Windows the swap fails while a reader (the web server, a virus scanner) holds the old file open: try again for a moment,
    // then write in place (the readers tolerate that: they keep what they had and look again).
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (SDL_RenamePath(tmp.c_str(), path.c_str())) return true;
        SDL_Delay(10u << attempt);
    }
    SDL_RemovePath(tmp.c_str());
    return SDL_SaveFile(path.c_str(), data.data(), data.size());
}

bool isFile(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> names;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<std::vector<std::string>*>(user)->push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &names);
    return names;
}

std::string withoutTrailingSlash(std::string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
    return path;
}

std::string stampedId(std::string_view prefix) {
    static unsigned long long lastMs = 0;
    static unsigned sequence = 0;
    SDL_Time t = 0;
    SDL_GetCurrentTime(&t);
    const unsigned long long ms = static_cast<unsigned long long>(t / 1000000);
    sequence = ms == lastMs ? sequence + 1 : 0;           // ids made by this program in the same millisecond still come out in order
    lastMs = ms;
    std::random_device rd;                                // and two programs (the app, the web server) never make the same one
    char buf[48];
    std::snprintf(buf, sizeof buf, "-%012llx-%04x-%04x", ms, sequence & 0xFFFF, static_cast<unsigned>(rd() & 0xFFFF));
    return std::string(prefix) + buf;
}

bool safeId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

}  // namespace gm::fs

namespace gm {

std::string nowIso() {
    SDL_Time t = 0;
    SDL_DateTime dt;
    if (!SDL_GetCurrentTime(&t) || !SDL_TimeToDateTime(t, &dt, false)) return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    return buf;
}

}  // namespace gm
