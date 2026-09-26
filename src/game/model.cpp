#include "game/model.h"

#include <cstdlib>

namespace gm {

namespace {
// Everything the rest of the app knows about a kind of content, in one place: the loader (its data file), "see"
// links, the pack counts in Settings and the pages of the GM app all read it from here.
struct KindInfo {
    Kind kind;
    const char* key;       // saved files, content keys: "<pack>/spell/<id>"
    const char* label;     // one of them: "Spell"
    const char* file;      // its data file in a pack ("spells.yaml"), and the word a "see" uses for the whole category
    const char* title;     // all of them: "Spells"
    const char* page;      // the id of the page that shows them
    bool catalog;          // that page is a list + detail of this kind alone, made from this row (catalog_module.cpp)
};
const KindInfo kKinds[kKindCount] = {
    {Kind::Monster, "monster", "Creature", "creatures", "Creatures", "creatures", true},
    {Kind::Spell, "spell", "Spell", "spells", "Spells", "spells", true},
    {Kind::Ability, "ability", "Ability", "abilities", "Abilities", "abilities", true},
    {Kind::Skill, "skill", "Skill", "skills", "Skills", "skills", true},
    {Kind::Kin, "kin", "Kin", "kin", "Kin", "kin", true},
    {Kind::Profession, "profession", "Profession", "professions", "Professions", "professions", true},
    // shown elsewhere: tables inside the rules, weapons / armor / gear as the book's own tables on the Gear page
    {Kind::Table, "table", "Table", "tables", "Tables", "rules", false},
    {Kind::Weapon, "weapon", "Weapon", "weapons", "Weapons", "gear", false},
    {Kind::Armor, "armor", "Armor", "armor", "Armor", "gear", false},
    {Kind::Gear, "gear", "Gear", "gear", "Gear", "gear", false},
};
const KindInfo& info(Kind k) { return kKinds[static_cast<int>(k)]; }
}  // namespace

const char* kindLabel(Kind k) { return info(k).label; }
const char* kindKey(Kind k) { return info(k).key; }
const char* kindFile(Kind k) { return info(k).file; }
const char* kindTitle(Kind k) { return info(k).title; }
const char* kindPage(Kind k) { return info(k).page; }

std::vector<Kind> kindsWithOwnPage() {
    std::vector<Kind> out;
    for (const KindInfo& k : kKinds)
        if (k.catalog) out.push_back(k.kind);
    return out;
}

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
