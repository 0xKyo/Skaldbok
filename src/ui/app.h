// The application shell: one window that shows the settings screen (content packs and the players' web server). Everything that
// shows game data is the web page, which the GM opens from here.
#pragma once

#include <memory>
#include <string>

#include "ui/host.h"
#include "ui/settings_screen.h"

struct SDL_Renderer;

namespace gm {

// Optional command-line driven start state.
struct StartupOptions {
    std::string importPath;           // a pack folder or .zip to install before anything is shown
};

class App : public Host {
public:
    App(SDL_Window* window, SDL_Renderer* renderer, Paths paths);
    ~App() override;

    void applyStartup(const StartupOptions& o);
    void frame(float width, float height);    // build the whole UI for one frame
    bool wantsRedraw() const;                 // something is animating or in flight
    // Looks at the content packs, which are read again when their files are edited (homebrew being written, Core being rebuilt). True if something changed, so the caller draws again. Cheap, and limited to a few times a second.
    bool pollExternal();

    // ---- Host ---------------------------------------------------------------------------------------
    const Paths& paths() const override { return paths_; }
    ContentStore& content() override { return content_; }
    Settings& settings() override { return settings_; }
    PackManager& packManager() override { return *packs_; }
    SDL_Window* window() override { return window_; }
    void contentChanged() override { reloadRequested_ = true; }
    void notify(const std::string& text) override;

private:
    void loadContent();
    void reloadContent();
    void setupStyle();

    SDL_Window* window_;
    SDL_Renderer* renderer_;
    Paths paths_;
    Settings settings_;
    ContentStore content_;
    std::unique_ptr<PackManager> packs_;
    double nextPoll_ = 0;
    double nextPackPoll_ = 0;
    std::string packSig_;                     // packSignature() of what is loaded now
    std::string pendingSig_;                  // a newer one seen once: it is loaded when it holds still for a look (a save may be in progress)
    bool reloadFromDisk_ = false;             // the pending reload comes from edited files, not from the app itself
    bool reloadRequested_ = false;

    std::unique_ptr<SettingsScreen> screen_;
    float zoom_ = 1.0f;
    std::string toast_;
    double toastUntil_ = 0;
};

}  // namespace gm
