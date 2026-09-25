// The user's choices that outlive a session: which content packs are off, and the web server's settings.
// One small JSON file in the per-user folder.
#pragma once

#include <set>
#include <string>

namespace gm {

// The players' web server: off until the GM starts it (Settings > Web server), or ticks "Launch server when opening".
struct WebSettings {
    bool launchOnOpen = false;                           // start it when the app opens
    int port = 8080;
    std::string publicUrl;                               // the address players type; empty = this computer's address on the network
};

class Settings {
public:
    void open(const std::string& file);                  // reads it if it exists; later changes are saved to it
    bool save() const;

    void setPackEnabled(const std::string& id, bool on);
    const std::set<std::string>& disabledPacks() const { return disabledPacks_; }

    WebSettings web;

private:
    std::string file_;
    std::set<std::string> disabledPacks_;
};

}  // namespace gm
