// Character creation following the Rulebook (chapter 2): the choices a player makes, checked and turned into a
// Character. The wizard in the UI only fills a Creation; everything that can be wrong is decided here so it can be tested.
#pragma once

#include <string>
#include <vector>

#include "character.h"
#include "content.h"
#include "dice.h"

namespace gm {

// One item of a starting gear set as printed: "Broadsword/battle axe/morning star" (a choice), "torch",
// "D6 food rations" or "D8 silver" (rolled).
struct GearPick {
    std::vector<std::string> options;      // more than one = the player picks
    int choice = 0;
    int diceSides = 0;                     // > 0: "D6 food rations"
    int rolled = 0;
    std::string what;                      // for dice picks: "food rations", "silver"
};

std::vector<GearPick> parseGearSet(const std::string& text);
// Which of the three printed sets a D6 roll selects (1-2, 3-4, 5-6).
int gearSetForRoll(int d6);
// Finds the weapon / armor / gear entry a gear name refers to ("small shield" -> "Shield, Small"), or nullptr.
const Entry* matchGear(const ContentStore& content, const std::string& name);

struct Creation {
    std::string kinKey, professionKey;
    std::string age = "adult";
    std::string school;                                    // only for professions with schools of magic (the mage)
    int rolled[kAttrCount] = {0, 0, 0, 0, 0, 0};           // score given to each attribute as rolled, before age (0 = not yet)
    std::vector<std::string> professionSkills;             // kProfessionSkills of them, from the profession's list
    std::vector<std::string> freeSkills;                   // the rest, any skill
    std::string heroicAbility;                             // name; not for mages
    std::vector<std::string> spells;                       // spell and trick keys (mages)
    int gearSet = -1;                                      // 0..2
    std::vector<GearPick> gear;                            // the picks of that set
    std::string name, nickname, player, weakness, memento, appearance;
};

// 4D6, the lowest die dropped.
int rollAttribute(Dice& dice);
// What is missing or wrong, in plain words. Empty = ready to build.
std::vector<std::string> validateCreation(const Creation& c, const ContentStore& content);
// The skills a profession offers for its first six (for a mage: those of the chosen school).
std::vector<std::string> professionSkillOptions(const Entry& profession, const std::string& school);
// Skills that may be picked freely: every skill with a single attribute.
std::vector<const Entry*> selectableSkills(const ContentStore& content);
// Spells a mage may choose: rank-1 spells (or tricks) of the school or General Magic.
std::vector<const Entry*> magicChoices(const ContentStore& content, const Entry& profession, const std::string& school, bool tricks);
// Builds the sheet. Dice picks that were not rolled yet are rolled now. Call validateCreation first.
Character buildCharacter(const Creation& c, const ContentStore& content, Dice& dice);
// Picks a random row's first cell from a table; empty if the table has no rows.
std::string rollTable(const DataTable& table, Dice& dice, int* rollOut = nullptr);
// Chooses the gear set (one of the profession's printed sets) and rolls its dice, so the picks are ready to show.
void chooseGearSet(Creation& c, const Entry& profession, int set, Dice& dice);
// A complete, valid random character made by the book's own rolls (kin, profession, age, 4D6 attributes, gear, names...).
Creation randomCreation(const ContentStore& content, Dice& dice);

}  // namespace gm
