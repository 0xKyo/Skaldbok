// Player characters: the data, the rules that derive numbers from it, and where the files live. No UI.
//
// A character is one JSON file (see docs/CHARACTERS.md). Everything it takes from the game content (kin, skills,
// abilities, spells, gear) is stored as a reference: the content's stable key plus its name at that moment, so a
// sheet still reads correctly if the pack it came from is later removed. A future web app can read the same files.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "jsondir.h"

namespace gm {

constexpr int kAttrCount = 6;
extern const char* const kAttrShort[kAttrCount];   // STR CON AGL INT WIL CHA
extern const char* const kAttrLong[kAttrCount];    // Strength ...
int attrIndex(const std::string& shortName);       // -1 if it is not one of the six

struct Ref {
    std::string key;                 // "<pack>/<kind>/<id>", empty if the thing was typed in by hand
    std::string name;
    bool empty() const { return key.empty() && name.empty(); }
};

struct Item {
    std::string name;
    std::string key;                 // links to a weapon / armor / gear entry when there is one
    int count = 1;
    std::string note;
};

struct SkillEntry {
    Ref ref;
    std::string attribute;           // STR..CHA, kept so the sheet works without the skill's pack
    int level = 0;                   // 0 = the base chance of its attribute
    bool trained = false;
    bool marked = false;             // advancement mark
};

// The GM's ruling on something the rules do not allow but the player did anyway (see sheet_edit.h).
struct Review {
    std::string status;              // approved | rejected
    std::string value;               // what was judged, so a further change asks again
    std::string at;
};

struct Character {
    int format = 1;
    int revision = 0;                // +1 on every save, by whichever program saves: readers can tell that something changed
    std::string id;                  // file name without .json
    std::string name, nickname, player;
    std::string age = "adult";       // young | adult | old
    Ref kin, profession;
    std::string school;              // a mage's school of magic
    int attr[kAttrCount] = {10, 10, 10, 10, 10, 10};
    int hp = 10, wp = 10;            // current values
    int hpBonus = 0, wpBonus = 0;    // heroic abilities such as Robust and Focused raise the maximums
    unsigned conditions = 0;         // bit i = the i-th condition of the book (Exhausted, Angry, ...)
    std::vector<SkillEntry> skills;
    std::vector<Ref> abilities;      // innate and heroic
    std::vector<Ref> spells;         // spells and magic tricks
    std::vector<Item> weapons;       // at hand (shields count)
    Item armor, helmet;
    std::vector<Item> inventory;
    std::vector<std::string> tinyItems;
    int gold = 0, silver = 0, copper = 0;
    std::string weakness, memento, appearance, notes;
    bool locked = false;                         // the GM switched the player's editing off
    std::map<std::string, Review> reviews;       // by issue key ("encumbrance", "attr:STR"...)
    std::string createdAt, updatedAt;            // ISO 8601, UTC

    std::string displayName() const;             // "Aria "Silvervoice""
    std::string summary() const;                 // the sheet as plain text (the creator's preview)
    std::string toJson() const;                  // pretty-printed, what is written to disk
    static bool fromJson(const std::string& text, Character& out, std::string* error);
};

// ------------------------------------------------------------------------------------------ rules
// Numbers from the Dragonbane Rulebook, chapter 2 (pages 28-29 of the printed book).

int baseChance(int attribute);                        // skill base chance: 1-5:3, 6-8:4, 9-12:5, 13-15:6, 16-18:7
int movementModifier(int agl);                        // AGL 1-6:-4, 7-9:-2, 10-12:0, 13-15:+2, 16-18:+4
std::string damageBonus(int strOrAgl);                // "" up to 12, "D4" 13-16, "D6" 17+
int encumbranceLimit(const Character& c);             // half STR rounded up, +2 with a backpack
int carriedItems(const Character& c);                 // rows of the inventory; food rations count one item per four
int maxHp(const Character& c);                        // CON (+ bonus)
int maxWp(const Character& c);                        // WIL (+ bonus)

struct AgeRule {
    const char* id;                  // young | adult | old
    const char* label;
    int trainedSkills;               // 8 / 10 / 12 (six of them from the profession)
    int attrMod[kAttrCount];         // added to the rolled attributes (never above 18)
    const char* summary;
};
const AgeRule& ageRule(const std::string& age);       // unknown text = adult
constexpr int kProfessionSkills = 6;                   // trained skills that must come from the profession

// Rolled attributes + age adjustment, each clamped to 1..18.
void applyAge(const int rolled[kAttrCount], const std::string& age, int out[kAttrCount]);

// Effective level of a skill: the recorded level, or the base chance of its attribute.
int skillLevel(const Character& c, const SkillEntry* entry, const std::string& attribute);

// ---------------------------------------------------------------------------------------- files

class CharacterStore : public JsonDirStore<Character> {
public:
    CharacterStore() : JsonDirStore('c', "character") {}
    bool importFile(const std::string& path, std::string* newId, std::string* error);   // gives it a new id
    bool exportFile(const Character& c, const std::string& path, std::string* error) const;

protected:
    // The web server edits these files too: a save changes only what differs from what this store held, on top of the file as it is.
    bool commit(Character& item, const Character* memory) override;
};

}  // namespace gm
