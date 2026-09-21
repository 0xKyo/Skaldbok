// A party: a named group of characters that play together. No UI.
// One JSON file per party (docs/CHARACTERS.md), next to the characters' files.
#pragma once

#include <string>
#include <vector>

#include "jsondir.h"

namespace gm {

struct Party {
    int format = 1;
    std::string id;                       // file name without .json
    std::string name;
    std::vector<std::string> members;     // character ids, in the order the GM keeps them
    std::string notes;
    std::string createdAt, updatedAt;

    bool has(const std::string& characterId) const;
    std::string toJson() const;
    static bool fromJson(const std::string& text, Party& out, std::string* error);
};

class PartyStore : public JsonDirStore<Party> {
public:
    PartyStore() : JsonDirStore('p', "party") {}

    const Party* partyOf(const std::string& characterId) const;   // the first party a character belongs to
    bool addMember(const std::string& partyId, const std::string& characterId);
    // A character was deleted: take it out of every party.
    void forgetCharacter(const std::string& characterId);
};

}  // namespace gm
