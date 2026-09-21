// The user's choices that outlive a session: which modules and content packs are on, and the web server's settings.
// One small JSON file in the per-user folder.
#pragma once

#include <map>
#include <set>
#include <string>

namespace gm {

// The players' web server, which the app starts along with itself.
struct WebSettings {
    bool autostart = true;                               // start it when the app opens
    int port = 8080;
    std::string publicUrl;                               // the address players type; empty = this computer's address on the network
};

class Settings {
public:
    void open(const std::string& file);                  // reads it if it exists; later changes are saved to it
    bool save() const;

    bool moduleEnabled(const std::string& id) const;     // modules are on unless switched off
    void setModuleEnabled(const std::string& id, bool on);
    void setPackEnabled(const std::string& id, bool on);
    const std::set<std::string>& disabledPacks() const { return disabledPacks_; }

    WebSettings web;

private:
    std::string file_;
    std::map<std::string, bool> modules_;
    std::set<std::string> disabledPacks_;
};

}  // namespace gm
