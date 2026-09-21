#include "model.h"

#include <cstdlib>

namespace gm {

namespace {
struct KindName {
    Kind kind;
    const char* key;
    const char* label;
};
const KindName kKinds[kKindCount] = {
    {Kind::Monster, "monster", "Creature"}, {Kind::Spell, "spell", "Spell"},
    {Kind::Ability, "ability", "Ability"},  {Kind::Skill, "skill", "Skill"},
    {Kind::Kin, "kin", "Kin"},              {Kind::Profession, "profession", "Profession"},
    {Kind::Table, "table", "Table"},        {Kind::Weapon, "weapon", "Weapon"},
    {Kind::Armor, "armor", "Armor"},        {Kind::Gear, "gear", "Gear"},
};
}  // namespace

const char* kindLabel(Kind k) { return kKinds[static_cast<int>(k)].label; }
const char* kindKey(Kind k) { return kKinds[static_cast<int>(k)].key; }

bool kindFromKey(const std::string& s, Kind& out) {
    for (const KindName& k : kKinds)
        if (s == k.key) {
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
