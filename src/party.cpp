#include "party.h"

#include <algorithm>

#include "jsonutil.h"

namespace gm {

bool Party::has(const std::string& characterId) const { return std::ranges::contains(members, characterId); }

std::string Party::toJson() const {
    json j;
    j["format"] = format;
    j["id"] = id;
    j["name"] = name;
    j["members"] = members;
    j["notes"] = notes;
    j["created_at"] = createdAt;
    j["updated_at"] = updatedAt;
    return j.dump(2) + "\n";
}

bool Party::fromJson(const std::string& text, Party& out, std::string* error) {
    json j;
    if (!jsonParse(text, j, error)) return false;
    if (!j.is_object()) {
        if (error) *error = "a party file must be a JSON object";
        return false;
    }
    Party p;
    p.format = jsonInt(j, "format", 1);
    p.id = jsonStr(j, "id");
    p.name = jsonStr(j, "name");
    for (const std::string& m : jsonStrings(j, "members"))
        if (!p.has(m)) p.members.push_back(m);                   // no character twice
    p.notes = jsonStr(j, "notes");
    p.createdAt = jsonStr(j, "created_at");
    p.updatedAt = jsonStr(j, "updated_at");
    out = std::move(p);
    return true;
}

const Party* PartyStore::partyOf(const std::string& characterId) const {
    const auto it = std::ranges::find_if(all(), [&](const Party& p) { return p.has(characterId); });
    return it == all().end() ? nullptr : &*it;
}

bool PartyStore::addMember(const std::string& partyId, const std::string& characterId) {
    const Party* p = find(partyId);
    if (!p || characterId.empty() || p->has(characterId)) return false;
    Party copy = *p;
    copy.members.push_back(characterId);
    return save(copy);
}

void PartyStore::forgetCharacter(const std::string& characterId) {
    for (Party p : std::vector<Party>(all()))          // (a copy: saving changes the list being walked)
        if (p.has(characterId)) {
            std::erase(p.members, characterId);
            save(p);
        }
}

}  // namespace gm
