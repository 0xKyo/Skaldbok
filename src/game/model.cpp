#include "game/model.h"

#include <cstdlib>

namespace gm {

namespace {
// Everything the rest of the app knows about a kind of content, in one place: the loader (its data file), "see"
// links, the pack counts in Settings and the pages of the GM app all read it from here.
struct KindInfo {
    Kind kind;
    const char* key;       // saved files, content keys: "<pack>/spell/<id>"
    const char* file;      // its data file in a pack ("spells.yaml"), and the word a "see" uses for the whole category
    const char* title;     // all of them: "Spells"
};
const KindInfo kKinds[kKindCount] = {
    {Kind::Monster, "monster", "creatures", "Creatures"},
    {Kind::Spell, "spell", "spells", "Spells"},
    {Kind::Ability, "ability", "abilities", "Abilities"},
    {Kind::Skill, "skill", "skills", "Skills"},
    {Kind::Kin, "kin", "kin", "Kin"},
    {Kind::Profession, "profession", "professions", "Professions"},
    {Kind::Table, "table", "tables", "Tables"},
    {Kind::Weapon, "weapon", "weapons", "Weapons"},
    {Kind::Armor, "armor", "armor", "Armor"},
    {Kind::Gear, "gear", "gear", "Gear"},
};
const KindInfo& info(Kind k) { return kKinds[static_cast<int>(k)]; }
}  // namespace

const char* kindKey(Kind k) { return info(k).key; }
const char* kindFile(Kind k) { return info(k).file; }
const char* kindTitle(Kind k) { return info(k).title; }

bool kindFromKey(const std::string& s, Kind& out) {
    for (const KindInfo& k : kKinds)
        if (s == k.key) {
            out = k.kind;
            return true;
        }
    return false;
}

bool kindFromFile(const std::string& s, Kind& out) {
    for (const KindInfo& k : kKinds)
        if (s == k.file) {
            out = k.kind;
            return true;
        }
    return false;
}

int DataTable::dieSides() const {
    if (dice.size() >= 2 && (dice[0] == 'D' || dice[0] == 'd')) return std::atoi(dice.c_str() + 1);
    return 0;
}

int DataTable::rowForRoll(int roll) const {
    for (size_t i = 0; i < rows.size(); ++i)
        if (roll >= rows[i].rollMin && roll <= rows[i].rollMax) return static_cast<int>(i);
    return -1;
}

}  // namespace gm
