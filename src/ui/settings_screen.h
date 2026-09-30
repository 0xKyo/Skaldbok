// The only screen of the desktop app: General Settings (content packs: import, on/off, remove) and the players' web server, which
// runs as its own program (skaldbok_web). Everything else lives on the web page, which the GM opens from here.
#pragma once

#include <string>

#include <SDL3/SDL.h>

#include "ui/host.h"
#include "ui/packs_panel.h"

namespace gm {

class SettingsScreen {
public:
    explicit SettingsScreen(Host& host);
    ~SettingsScreen();
    SettingsScreen(const SettingsScreen&) = delete;
    SettingsScreen& operator=(const SettingsScreen&) = delete;

    void update();                     // once per frame: the file dialog, and the server's status
    void draw();
    bool wantsRedraw() const;          // a dialog is open or the server is starting: keep the frames coming

private:
    struct Status {
        bool running = false;
        std::string url, localUrl, gmLink, error;
    };

    std::string statusFile() const;
    void readStatus();
    void start();
    void stop();

    Host& host_;
    PacksPanel packs_;
    SDL_Process* proc_ = nullptr;
    Status status_;
    std::string message_;
    Uint64 watchUntil_ = 0, nextRead_ = 0;
    char port_[8] = {};
    char publicUrl_[200] = {};
};

}  // namespace gm
