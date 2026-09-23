// Settings of the player-facing web server, from the environment. Every path can be overridden.
#pragma once

#include <string>

namespace gm {

struct WebConfig {
    int port = 8080;
    std::string host = "0.0.0.0";
    std::string prefsDir;                 // the GM app's per-user folder: characters/, parties/, packs/, settings.json
    std::string dataDir;                  // data/: packs/core
    std::string publicUrl;                // the address players type; written to web-access.json for the GM app's links
    std::string staticDir;                // the built Vue client
    int maxFailures = 20;                 // failed token attempts allowed per address in the window
    long long failureWindowMs = 10 * 60 * 1000;
    long long refreshMs = 750;            // how long a look at the files is trusted
    bool trustProxy = false;              // take the client address from X-Forwarded-For (behind a tunnel or reverse proxy)

    static WebConfig fromEnvironment();
    static std::string defaultPrefsDir();
    // This computer's address on the local network ("192.168.1.20"), or "" if it has none. Players' phones need it, not "localhost".
    static std::string lanAddress();
    // http://<lan address>:<port>, or http://localhost:<port> when there is no network.
    static std::string defaultPublicUrl(int port);
};

}  // namespace gm
