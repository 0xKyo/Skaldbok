// What the browser is shown. Everything a player may see is built here, field by field, from the GM app's own data:
// nothing is passed through wholesale, so a new field in a character file stays private until it is added on purpose.
// Creatures are the GM's and are never offered to players. The tables of the rules, cards and intros travel with their text.
#pragma once

#include <functional>
#include <string>

#include "game/character.h"
#include "parsing/content.h"
#include "parsing/jsonutil.h"
#include "game/messages.h"

namespace gm {

// The player's own sheet, complete, with the numbers derived.
// `asGm`: the editable document also carries kin, profession and school, which only the GM may change.
json characterView(const Character& c, const ContentStore& content, bool asGm = false);

// Compact summary of one character for the GM's character list.
json characterSummary(const Character& c, const ContentStore& content, const std::string& link);

// The Reference: everything under it is the same for the GM and for the players (types, rule chapters, creatures, the NPC lists).
json contentSummary(const ContentStore& content);
bool contentList(const ContentStore& content, const std::string& typeId, const std::string& query, json& out);
bool rulesChapter(const ContentStore& content, const std::string& key, json& out);
json npcLists(const ContentStore& content);   // the random NPC generator's lists
json monsterList(const ContentStore& content, const std::string& query);
// `prefix` is where the picture is served from: "/gm" for the GM's routes, "" for the players'.
json monsterDetail(const ContentStore& content, const Monster& m, const std::string& prefix);

json publicCard(const ContentStore& content, const Entry& e);

}  // namespace gm
