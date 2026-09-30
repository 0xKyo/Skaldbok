// What the settings screen needs from the app shell: where the files are, the loaded content and settings, and a few services.
// The desktop app has no views of the data any more (the web client shows everything); it manages content packs and the web server.
#pragma once

#include <string>

#include "game/settings.h"
#include "parsing/content.h"
#include "parsing/packs.h"

struct SDL_Window;

namespace gm {

struct Paths {
    std::string root;        // project root: holds data/ and References/
    std::string dataDir;
    std::string prefDir;     // per-user files, ends with '/': settings, imported packs, web files
    std::string coreDir() const { return dataDir + "/packs/core"; }
    std::string userPacksDir() const { return prefDir + "packs"; }
};

class Host {
public:
    virtual ~Host() = default;

    virtual const Paths& paths() const = 0;
    virtual ContentStore& content() = 0;
    virtual Settings& settings() = 0;
    virtual PackManager& packManager() = 0;
    virtual SDL_Window* window() = 0;

    virtual void contentChanged() = 0;                   // after packs changed: read everything again
    virtual void notify(const std::string& text) = 0;    // a short message at the bottom
};

}  // namespace gm
