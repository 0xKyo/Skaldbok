#include "web/web_config.h"

#include "content.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <vector>

#include <SDL3/SDL.h>

namespace gm {
namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

std::string normalize(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

bool exists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info);
}

int envInt(const char* name, int def) {
    const std::string v = env(name);
    return v.empty() ? def : std::atoi(v.c_str());
}

}  // namespace

// The folder the GM app keeps the user's files in: the same one SDL gives it (SDL_GetPrefPath("skaldbok", "gm")).
std::string WebConfig::defaultPrefsDir() {
    if (char* pref = SDL_GetPrefPath("skaldbok", "gm")) {
        std::string dir = normalize(pref);
        SDL_free(pref);
        return dir;
    }
    return "user";
}

// Asks the operating system which local address it would use to reach the outside. A UDP "connect" sends nothing: it only
// makes the system pick the network interface, and that interface's address is the one players on the same network can reach.
std::string WebConfig::lanAddress() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return {};
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return {};
#else
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return {};
#endif
    std::string out;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &target.sin_addr);                   // a documentation address: never routed anywhere
    if (connect(s, reinterpret_cast<sockaddr*>(&target), sizeof target) == 0) {
        sockaddr_in local{};
#ifdef _WIN32
        int len = sizeof local;
#else
        socklen_t len = sizeof local;
#endif
        if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
            char buf[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof buf) && std::string(buf) != "0.0.0.0" && std::string(buf).rfind("127.", 0) != 0) out = buf;
        }
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return out;
}

std::string WebConfig::defaultPublicUrl(int port) {
    const std::string lan = lanAddress();
    return "http://" + (lan.empty() ? std::string("localhost") : lan) + ":" + std::to_string(port);
}

WebConfig WebConfig::fromEnvironment() {
    WebConfig c;
    c.port = envInt("PORT", c.port);
    if (!env("HOST").empty()) c.host = env("HOST");
    c.prefsDir = normalize(env("SKALDBOK_PREFS").empty() ? defaultPrefsDir() : env("SKALDBOK_PREFS"));

    std::vector<std::string> data;
    if (!env("SKALDBOK_DATA").empty()) data.push_back(env("SKALDBOK_DATA"));
    std::vector<std::string> statics;
    if (!env("STATIC_DIR").empty()) statics.push_back(env("STATIC_DIR"));
    if (const char* base = SDL_GetBasePath()) {
        const std::string b = normalize(base);
        for (const char* rel : {"/data", "/../data", "/../../data", "/../../../data"}) data.push_back(b + rel);
        for (const char* rel : {"/web", "/../web", "/../../web/client/dist", "/../../../web/client/dist"}) statics.push_back(b + rel);
    }
#ifdef SKALDBOK_DEV_DATA_DIR
    data.push_back(SKALDBOK_DEV_DATA_DIR);
#endif
#ifdef SKALDBOK_SOURCE_DIR
    statics.push_back(std::string(SKALDBOK_SOURCE_DIR) + "/web/client/dist");
#endif
    c.dataDir = data.empty() ? "data" : normalize(data.front());
    for (const std::string& d : data)
        if (looksLikePack(normalize(d) + "/packs/core")) {
            c.dataDir = normalize(d);
            break;
        }
    c.staticDir = statics.empty() ? "web" : normalize(statics.front());
    for (const std::string& d : statics)
        if (exists(normalize(d) + "/index.html")) {
            c.staticDir = normalize(d);
            break;
        }
    c.publicUrl = env("PUBLIC_URL").empty() ? defaultPublicUrl(c.port) : env("PUBLIC_URL");
    while (!c.publicUrl.empty() && c.publicUrl.back() == '/') c.publicUrl.pop_back();
    c.maxFailures = envInt("MAX_FAILURES", c.maxFailures);
    c.failureWindowMs = envInt("FAILURE_WINDOW_MS", static_cast<int>(c.failureWindowMs));
    c.refreshMs = envInt("REFRESH_MS", static_cast<int>(c.refreshMs));
    c.trustProxy = env("TRUST_PROXY") == "1" || env("TRUST_PROXY") == "true";
    return c;
}

}  // namespace gm
