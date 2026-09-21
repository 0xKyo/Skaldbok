// skaldbok_web: the players' web server. Reads the GM app's files and shows each player their own character.
//
//   skaldbok_web                          serve (PORT, HOST, PUBLIC_URL, SKALDBOK_PREFS, SKALDBOK_DATA, STATIC_DIR...)
//   skaldbok_web --links                  print every player's personal link (creating missing tokens)
//   skaldbok_web --regenerate <name|id>   give that character a new link; the old one stops working at once
//
// Options (also as environment variables): --port N  --host H  --public-url URL  --prefs DIR  --data DIR  --static DIR
//                                          --parent PID   (exit when that process ends; the GM app uses it)
//                                          --trust-proxy  (behind a tunnel or reverse proxy: use X-Forwarded-For as the client address)
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <cerrno>
#endif

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <SDL3/SDL.h>

#include "jsonutil.h"
#include "web/web_app.h"
#include "web/web_server.h"

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

// True while the process that started us is still there. The GM app passes its own id, so a server can never outlive the app
// that launched it (even if the app is killed or crashes and never gets to stop us).
bool processAlive(long long pid) {
#ifdef _WIN32
    void* h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH;
#endif
}

// The GM app reads this to show whether the server is up and at which address (it deletes the file when it stops us).
void writeStatus(const gm::WebConfig& config, bool running, const std::string& error) {
    gm::json j = {{"running", running},
                  {"port", config.port},
                  {"url", config.publicUrl},
                  {"local_url", "http://localhost:" + std::to_string(config.port)},
                  {"error", error}};
    const std::string text = j.dump(2) + "\n";
    const std::string file = config.prefsDir + "/web-status.json";
    SDL_SaveFile(file.c_str(), text.data(), text.size());
}
std::string lowerText(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

int main(int argc, char** argv) {
    gm::WebConfig config = gm::WebConfig::fromEnvironment();
    bool links = false;
    std::string regenerate;
    bool publicUrlGiven = false;
    long long parentPid = 0;                              // --parent <pid>: exit when that process ends
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--links") links = true;
        else if (a == "--regenerate") regenerate = next();
        else if (a == "--port") config.port = std::atoi(next().c_str());
        else if (a == "--host") config.host = next();
        else if (a == "--prefs") config.prefsDir = next();
        else if (a == "--data") config.dataDir = next();
        else if (a == "--static") config.staticDir = next();
        else if (a == "--parent") parentPid = std::atoll(next().c_str());
        else if (a == "--trust-proxy") config.trustProxy = true;
        else if (a == "--public-url") {
            config.publicUrl = next();
            publicUrlGiven = true;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 64;
        }
    }
    if (!publicUrlGiven && std::getenv("PUBLIC_URL") == nullptr) config.publicUrl = gm::WebConfig::defaultPublicUrl(config.port);

    gm::WebApp app(config);
    if (!app.ok()) {
        std::fprintf(stderr, "%s\nRun python tools/build_db.py first, or point --data at the folder with skaldbok.db.\n", app.error().c_str());
        return 2;
    }
    const std::vector<gm::Character> characters = app.characters();

    if (!regenerate.empty()) {
        const std::string want = lowerText(regenerate);
        const gm::Character* target = nullptr;
        for (const gm::Character& c : characters)
            if (c.id == regenerate || lowerText(c.name) == want) target = &c;
        if (!target) {
            std::fprintf(stderr, "No character called \"%s\".\n", regenerate.c_str());
            return 1;
        }
        app.access().regenerate(target->id);
        std::printf("New link for %s: the old one no longer works.\n", target->name.c_str());
        links = true;
    }
    if (links) {
        if (characters.empty()) std::printf("No characters found in %s. Create them in the GM app first.\n", config.prefsDir.c_str());
        for (const gm::Character& c : characters) {
            const auto [name, link] = app.nameAndLink(c);
            std::printf("%-24s %s\n", name.c_str(), link.c_str());
        }
        std::printf("\nLinks use %s (set PUBLIC_URL to the address your players use).\n", config.publicUrl.c_str());
        return 0;
    }

    gm::WebServer server(app);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    // A copy that is still shutting down (the GM closed the app and opened it again at once) frees the port within a moment: wait for it.
    int bound = 0;
    for (int attempt = 0; attempt < 16 && bound <= 0 && !g_stop; ++attempt) {
        bound = server.start(config.host, config.port);
        if (bound <= 0) SDL_Delay(250);
    }
    if (bound <= 0) {
        const std::string why = "Port " + std::to_string(config.port) + " is not available (is another copy of the web server already running?)";
        std::fprintf(stderr, "Cannot listen on %s:%d. %s\n", config.host.c_str(), config.port, why.c_str());
        writeStatus(config, false, why);
        return 3;
    }
    writeStatus(config, true, "");
    std::printf("Skaldbok web listening on %s (http://%s:%d)\n", config.publicUrl.c_str(), config.host.c_str(), config.port);
    std::printf("  GM files:   %s\n  Books/Core: %s\n  Client:     %s\n", config.prefsDir.c_str(), config.dataDir.c_str(), config.staticDir.c_str());
    std::printf("  Players:    %d character(s); their links: skaldbok_web --links (or in the GM app, Characters)\n", static_cast<int>(characters.size()));
    std::fflush(stdout);
    while (!g_stop) {
        SDL_Delay(500);
        if (parentPid > 0 && !processAlive(parentPid)) break;         // the app that started us is gone
    }
    server.stop();
    const std::string file = config.prefsDir + "/web-status.json";
    SDL_RemovePath(file.c_str());
    return 0;
}
