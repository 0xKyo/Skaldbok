// The GM's own homebrew pack ("custom", in the user's packs folder), which the GM page writes: creatures so far. The pack is created the first
// time something is saved, and what the GM makes goes into its data files like any pack's (docs/HOMEBREW.md), so nothing here is special to the
// loader: the server reads the pack again after every change and the creature shows up in the Reference.
#pragma once

#include <string>

#include "parsing/jsonutil.h"

namespace gm {

struct HomebrewResult {
    int status = 200;
    std::string error;             // when status is not 2xx
    json body;
};

// Where the GM's pack lives inside the GM app's per-user folder.
std::string customPackDir(const std::string& prefsDir);

// The creatures of the GM's pack, as stored: {"creatures": [{id, key, name, kind, ...}]}.
json homebrewCreatures(const std::string& packDir);

// Adds a creature (`id` empty) or replaces the one with that id. The fields are checked and trimmed; what is not a creature's field is dropped.
HomebrewResult saveHomebrewCreature(const std::string& packDir, const std::string& id, const json& body);

HomebrewResult deleteHomebrewCreature(const std::string& packDir, const std::string& id);

}  // namespace gm
