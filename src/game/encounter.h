// The GM's combat tracker: who is fighting, their hit points, conditions and turn order. No UI in here.
#pragma once

#include <string>
#include <vector>

#include "game/dice.h"

namespace gm {

// The six conditions of Dragonbane, in the book's order. Bit i of Combatant::conditions is kConditions[i].
struct ConditionInfo {
    const char* name;
    const char* attribute;    // the attribute (and its skills) that get a bane
};
extern const ConditionInfo kConditions[6];

struct Combatant {
    int uid = 0;                // stable while the app runs (not saved); lets the UI keep its selection across sorts
    int monsterId = 0;          // 0 = a player character or a custom entry; a handle, valid while the content stays loaded
    std::string monsterKey;     // the creature's stable content key: what is saved (handles change when packs do)
    std::string characterId;    // set for player characters added from the Characters module
    std::string name;
    int hp = 0, maxHp = 0;
    int initiative = 0;         // card 1..10, lower acts first; 0 = not drawn yet
    unsigned conditions = 0;
    std::string note;
    bool alive() const { return hp > 0; }
};

class Encounter {
public:
    std::vector<Combatant> combatants;
    int round = 1;
    int active = -1;            // index into `combatants`, -1 = combat has not started

    int add(Combatant c);                       // returns the new index
    int indexOfUid(int uid) const;              // -1 if it is gone
    void remove(int index);
    void clear();
    void changeHp(int index, int delta);        // never below 0 nor above max
    void drawInitiative(Dice& dice);            // everyone draws a card; unique while there are 10 or fewer
    void sortByInitiative();                    // stable: ties keep their order; undrawn go last
    void start();                               // sort and make the first combatant active
    void nextTurn();                            // advances; a new round begins after the last one

    std::string serialize() const;
    static Encounter parse(const std::string& text);

    // First number in text ("28", "2D6+3" -> 2); 0 if none. Used to read HP out of stat blocks.
    static int firstNumber(const std::string& text);

private:
    int nextUid_ = 1;
};

}  // namespace gm
