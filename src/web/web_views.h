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
#include "game/party.h"

namespace gm {

// The player's own sheet, complete, with the numbers derived (party may be null).
json characterView(const Character& c, const ContentStore& content, const Party* party);

// Compact summary of one character for the GM's character list.
json characterSummary(const Character& c, const ContentStore& content, const std::string& link);

// The party as its own members see it: who is in it and how they are doing. Skills, gear and notes stay private.
json partyView(const Party& party, const std::function<const Character*(const std::string&)>& findCharacter, const std::string& meId);

// Content visible to players.
json contentSummary(const ContentStore& content);
bool contentList(const ContentStore& content, const std::string& typeId, const std::string& query, json& out);
bool rulesChapter(const ContentStore& content, const std::string& key, json& out);

// GM-only content: includes creatures count and web_hide chapters/nodes.
json contentSummaryGm(const ContentStore& content);
bool rulesChapterGm(const ContentStore& content, const std::string& key, json& out);
json npcLists(const ContentStore& content);   // the random NPC generator's lists
json monsterList(const ContentStore& content, const std::string& query);
json monsterDetail(const ContentStore& content, const Monster& m);

json publicCard(const ContentStore& content, const Entry& e);

}  // namespace gm
