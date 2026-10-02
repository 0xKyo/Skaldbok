// The adventures the GM writes on the Homebrew page. Each one is a pack of its own in the user's packs folder (<prefs>/packs/<id>/) with a manifest and an
// adventure.yaml, which is exactly what the Adventures tab already reads (web_adventures.*): the GM's adventure shows up there like the book's. The
// editor works on the whole tree: {"title": ..., "chapters": [node]}, a node being {id, title, kind, number?, body?, images?: ["images/..."], creatures?: [keys],
// sections?: [node]}. What is written is checked and cleaned (only those fields, bounded sizes and depth); other files of the pack are left as they are.
#pragma once

#include <string>
#include <string_view>

#include "web/web_homebrew.h"

namespace gm {

// {"adventures": [{id, title, chapters}]}: the packs of the user's folder that have an adventure.yaml, by title.
json ownAdventures(const std::string& prefsDir);
// A new, empty adventure with that title (its id is the title as a slug, made unique): {id, title}.
HomebrewResult createAdventure(const std::string& prefsDir, const std::string& title);
// {id, title, chapters} as stored; null when the user has no such adventure.
json readAdventure(const std::string& prefsDir, const std::string& id);
// Writes the title and the tree (400 when they are not what an adventure is made of).
HomebrewResult saveAdventure(const std::string& prefsDir, const std::string& id, const json& body);
// Removes the adventure's pack, its pictures included.
HomebrewResult deleteAdventure(const std::string& prefsDir, const std::string& id);
// Keeps a picture (PNG, JPEG or WebP, 8 MB at most) in the adventure: `body["path"]` is the path to put in a node's `images`.
HomebrewResult saveAdventureImage(const std::string& prefsDir, const std::string& id, std::string_view bytes);

}  // namespace gm
