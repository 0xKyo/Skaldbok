// What the browser is shown. Everything a player may see is built here, field by field, from the GM app's own data:
// nothing is passed through wholesale, so a new field in a character file stays private until it is added on purpose.
// Creatures and tables are the GM's and are never offered.
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

// The party as its own members see it: who is in it and how they are doing. Skills, gear and notes stay private.
json partyView(const Party& party, const std::function<const Character*(const std::string&)>& findCharacter, const std::string& meId);

// The rules a player may look up.
json contentSummary(const ContentStore& content);
bool contentList(const ContentStore& content, const std::string& typeId, const std::string& query, json& out);

json publicCard(const ContentStore& content, const Entry& e);

}  // namespace gm
