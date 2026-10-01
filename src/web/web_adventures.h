// The adventures the GM can read: a pack that has an adventure.yaml (docs/WEB.md, tools/mistyvale.py makes one from a book). The book's own
// packs live in <data>/packs/<id>, the user's in <prefs>/packs/<id>; one in the user's folder wins over a book's of the same id. Nothing here
// is part of the content store: the adventure is a document that is read, its creatures and tables point at what the store already has.
#pragma once

#include <string>

#include "parsing/content.h"
#include "parsing/jsonutil.h"

namespace gm {

// {"adventures": [{id, name, title, chapters}]}, by name.
json adventureList(const std::string& dataDir, const std::string& prefsDir);

// {"id", "name", "title", "chapters": [...nodes...], "tables": {name: {title, dice, columns, rows}}, "creatures": {key: {name, kind}}}; null when there is no such adventure.
json adventureDetail(const std::string& dataDir, const std::string& prefsDir, const std::string& id, const ContentStore& content);

// The file of a picture of an adventure ("images/maps/outskirt.jpg", inside the pack's images folder); "" when the path is not one of those or
// the file is not there. The path comes from the request, so only a plain relative path to a picture under images/ is accepted.
std::string adventureImagePath(const std::string& dataDir, const std::string& prefsDir, const std::string& id, const std::string& relative);

// The GM's own notes on an adventure's nodes (an NPC...), kept in the GM's folder, never in the pack: {node id: text}.
json adventureNotes(const std::string& prefsDir, const std::string& id);

struct NoteResult {
    int status = 200;
    std::string error;
};

// Sets the note of a node (an empty text removes it). `id` must be an adventure that exists.
NoteResult saveAdventureNote(const std::string& dataDir, const std::string& prefsDir, const std::string& id, const std::string& node, const std::string& text);

}  // namespace gm
