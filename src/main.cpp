#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "app.h"
#include "fonts.h"

namespace {

struct Options {
    std::string dataDir;
    std::string prefDir;            // --prefs <folder>: keep pins/recents/encounter here (tests use a scratch folder)
    std::string shot;               // write a PNG of the window and quit
    int width = 1360, height = 860;
    int shotFrames = 6;
    bool software = false;
    gm::StartupOptions startup;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--data") o.dataDir = next();
        else if (a == "--shot") o.shot = next();
        else if (a == "--frames") o.shotFrames = std::max(2, std::atoi(next().c_str()));
        else if (a == "--tab") o.startup.tab = next();
        else if (a == "--select") o.startup.select = next();
        else if (a == "--search") o.startup.search = next();
        else if (a == "--roll") o.startup.demoRoll = true;
        else if (a == "--page") o.startup.page = std::atoi(next().c_str());
        else if (a == "--prefs") o.prefDir = next();
        else if (a == "--demo-encounter") o.startup.demoEncounter = true;
        else if (a == "--demo-character") o.startup.demoCharacter = true;
        else if (a == "--demo-party") o.startup.demoParty = true;
        else if (a == "--demo-screen") o.startup.demoScreen = true;
        else if (a == "--no-web") SDL_setenv_unsafe("SKALDBOK_NO_WEB", "1", 1);      // do not start the players' web server
        else if (a == "--new-character") o.startup.newCharacter = std::atoi(next().c_str());     // the step to open (0 = first)
        else if (a == "--import") o.startup.importPath = next();
        else if (a == "--software") o.software = true;
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
    }
    return o;
}

bool fileExists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string normalize(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string parentDir(const std::string& p) {
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

// data/ is found next to the executable, one or two levels above it (build trees), or via --data / $SKALDBOK_DATA.
bool locateData(const Options& o, gm::Paths& out) {
    std::vector<std::string> candidates;
    if (!o.dataDir.empty()) candidates.push_back(o.dataDir);
    if (const char* env = std::getenv("SKALDBOK_DATA")) candidates.push_back(env);
    if (const char* base = SDL_GetBasePath()) {
        const std::string b = normalize(base);
        candidates.push_back(b + "/data");
        candidates.push_back(b + "/../data");
        candidates.push_back(b + "/../../data");
        candidates.push_back(b + "/../../../data");
    }
#ifdef SKALDBOK_DEV_DATA_DIR
    candidates.push_back(SKALDBOK_DEV_DATA_DIR);
#endif
    for (std::string c : candidates) {
        c = normalize(c);
        if (fileExists(c + "/dragonbane.db")) {
            out.dataDir = c;
            out.dbPath = c + "/dragonbane.db";
            out.root = parentDir(c);
            return true;
        }
    }
    return false;
}

void fail(const std::string& message) {
    std::fprintf(stderr, "%s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Skaldbok", message.c_str(), nullptr);
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);

    gm::Paths paths;
    if (!locateData(opt, paths)) {
        fail("Cannot find data/dragonbane.db.\nRun python tools/build_db.py, or start with --data <folder>.");
        return 2;
    }
    paths.prefDir = normalize(opt.prefDir);
    if (!fileExists(paths.coreDir() + "/manifest.json")) {
        fail("Cannot find the Core content pack (data/packs/core).\nRun python tools/build_db.py (or python tools/export_packs.py).");
        return 2;
    }
    gm::Database db;
    std::string err;
    if (!db.open(paths.dbPath, &err)) {
        fail("Cannot open " + paths.dbPath + ":\n" + err);
        return 2;
    }

    if (opt.software) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fail(std::string("SDL could not start: ") + SDL_GetError());
        return 3;
    }
    SDL_Window* window = SDL_CreateWindow("Skaldbok", opt.width, opt.height,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        fail(std::string("Cannot create a window: ") + SDL_GetError());
        return 3;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowMinimumSize(window, 900, 560);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                     // layout is fixed; nothing to persist
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    gm::setupFonts();

    int exitCode = 0;
    {
        gm::App app(window, renderer, db, paths);
        const float scale = SDL_GetWindowDisplayScale(window);
        ImGui::GetStyle().ScaleAllSizes(scale > 0 ? scale : 1.0f);
        ImGui::GetStyle().FontScaleDpi = scale > 0 ? scale : 1.0f;
        app.applyStartup(opt.startup);

        bool running = true;
        int frames = 3;                            // frames still owed after the last input
        Uint64 settleUntil = SDL_GetTicks() + 800; // keep drawing briefly after input so hover tooltips can appear
        bool wasAnimating = false;
        int shotCountdown = opt.shot.empty() ? -1 : opt.shotFrames;
        while (running) {
            const bool animating = app.wantsRedraw();
            if (wasAnimating && !animating) frames = std::max(frames, 2);   // one last frame to clear a finished flash/toast
            wasAnimating = animating;
            const bool idle = frames <= 0 && shotCountdown < 0 && !animating && SDL_GetTicks() >= settleUntil;
            SDL_Event ev;
            bool any = false;
            auto handle = [&](const SDL_Event& e) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
                if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) app.dropFile(e.drop.data, e.drop.x, e.drop.y);
                any = true;
            };
            if (idle) {
                // Nothing moves: sleep until input arrives. Only a focused text box needs periodic frames (caret blink).
                const bool got = SDL_WaitEventTimeout(&ev, 300);        // (a player may be changing their sheet: look at the files now and then)
                if (got) handle(ev);
                else if (io.WantTextInput) frames = 1;                  // the caret of a focused text box blinks
            }
            while (SDL_PollEvent(&ev)) handle(ev);
            if (app.pollExternal()) frames = std::max(frames, 3);
            if (any) {
                frames = 3;
                settleUntil = SDL_GetTicks() + 800;
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(100);
                continue;
            }
            if (frames <= 0 && shotCountdown < 0) continue;

            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.frame(io.DisplaySize.x, io.DisplaySize.y);
            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 231, 223, 204, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            if (shotCountdown == 0) {
                SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr);
                if (s && SDL_SavePNG(s, opt.shot.c_str())) std::printf("saved %s\n", opt.shot.c_str());
                else { std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError()); exitCode = 4; }
                SDL_DestroySurface(s);
                running = false;
            }
            SDL_RenderPresent(renderer);
            if (shotCountdown > 0) --shotCountdown;
            if (frames > 0) --frames;
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
