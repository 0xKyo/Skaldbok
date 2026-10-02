// The GM's own homebrew pack (in the user's packs folder, named as the GM likes; "homebrew" by default), which the GM page writes: creatures and an
// entry of every kind of data file of Core (abilities, spells, skills, equipment, tables...). The pack is created the first time something is
// saved, and what the GM makes goes into its data files like any pack's (docs/HOMEBREW.md), so nothing here is special to the loader: the server
// reads the pack again after every change and the entry shows up in the Reference.
#pragma once

#include <string>
#include <string_view>

#include "parsing/jsonutil.h"

namespace gm {

struct HomebrewResult {
    int status = 200;
    std::string error;             // when status is not 2xx
    json body;
};

// The pack's name as the GM chose it (default "Homebrew"), and where it lives inside the GM app's per-user folder (its id is the name as a slug).
std::string homebrewPackName(const std::string& prefsDir);
std::string customPackDir(const std::string& prefsDir);
HomebrewResult setHomebrewPackName(const std::string& prefsDir, const std::string& name);

// What the Homebrew tab can make: {"pack": {id, name}, "sections": [{id, label, fields: [{key, label, type, options?, cols?}]}]}. The field types are
// text, long, number, bool, select, list (of texts), pairs (label -> value) and rows (a list of objects with the fields of `cols`). `coreDir` is Core's
// folder: the files of tables take their header (kind, categories) from there.
json homebrewSchema(const std::string& prefsDir, const std::string& coreDir);

// The entries of one section as stored, each with its "index": {"entries": [...]}. Null when the section does not exist.
json homebrewEntries(const std::string& packDir, const std::string& coreDir, const std::string& section);

// Adds an entry (`index` -1) or replaces the one at `index` (`expect` is the name it must have: the list may have changed meanwhile).
HomebrewResult saveHomebrewEntry(const std::string& packDir, const std::string& coreDir, const std::string& section, int index, const std::string& expect, const json& body,
                                 const std::string& replaces = std::string(), const json& base = nullptr);
// The packs one can look into (the GM's own, Core, any other): {"packs": [{id, name, editable}]}; where one lives ("" if there is none); and the
// entry of a pack with a name, as stored, to start a house rule from (null if there is none). Only the GM's own pack can be written.
json homebrewPacks(const std::string& prefsDir, const std::string& coreDir);
std::string homebrewPackDir(const std::string& prefsDir, const std::string& coreDir, const std::string& id);
json homebrewOriginal(const std::string& dir, const std::string& coreDir, const std::string& section, const std::string& name);

HomebrewResult deleteHomebrewEntry(const std::string& packDir, const std::string& coreDir, const std::string& section, int index, const std::string& expect);

// The creatures of the GM's pack, as stored: {"creatures": [{id, key, name, kind, ...}]}.
json homebrewCreatures(const std::string& packDir);

// Adds a creature (`id` empty) or replaces the one with that id. The fields are checked and trimmed; what is not a creature's field is dropped.
HomebrewResult saveHomebrewCreature(const std::string& packDir, const std::string& id, const json& body);

HomebrewResult deleteHomebrewCreature(const std::string& packDir, const std::string& id);

// Keeps a picture for a creature (PNG, JPEG or WebP, 8 MB at most) in the pack's images/creatures folder: `body["path"]` is what the creature's "image" says.
HomebrewResult saveHomebrewCreatureImage(const std::string& packDir, std::string_view bytes);

}  // namespace gm
