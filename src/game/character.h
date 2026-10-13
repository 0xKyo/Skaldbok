// Player characters: the data, the rules that derive numbers from it, and where the files live. No UI.
//
// A character is one JSON file (see docs/CHARACTERS.md). Everything it takes from the game content (kin, skills,
// abilities, spells, gear) is stored as a reference: the content's stable key plus its name at that moment, so a
// sheet still reads correctly if the pack it came from is later removed. A future web app can read the same files.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "game/attributes.h"
#include "game/model.h"
#include "parsing/jsondir.h"

namespace gm {

// The attributes the rules of this game system are written for (data/system/attributes.yaml lists the attributes; a rule that needs a
// particular one names it by id). Everything the program stores about an attribute uses its id.
inline constexpr const char* kStrength = "strength";
inline constexpr const char* kConstitution = "constitution";
inline constexpr const char* kAgility = "agility";
inline constexpr const char* kIntelligence = "intelligence";
inline constexpr const char* kWillpower = "willpower";
inline constexpr const char* kCharisma = "charisma";
constexpr int kDefaultScore = 10;                       // what an attribute a sheet does not give is worth

// Files written before attributes had ids (character format 1) used the codes: "STR" -> "strength". Anything else is returned as it is.
std::string legacyAttributeId(const std::string& code);

// Attribute id -> score, in the order they were given.
class Scores {
public:
    int get(const std::string& id, int otherwise = kDefaultScore) const;
    void set(const std::string& id, int score);
    bool has(const std::string& id) const;
    const std::vector<std::pair<std::string, int>>& items() const { return items_; }
    bool operator==(const Scores& o) const { return items_ == o.items_; }

private:
    std::vector<std::pair<std::string, int>> items_;
};

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
    double weight = -1;              // encumbrance of one; negative = the usual (1, or 1/4 for food rations). 0 = weighs nothing
};

struct SkillEntry {
    Ref ref;
    std::string attribute;           // the id of its attribute ("strength"), kept so the sheet works without the skill's pack
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

constexpr int kCharacterFormat = 2;      // 2: attributes and skills name their attribute by id (1 used the codes STR, CON...)

struct Character {
    int format = kCharacterFormat;
    int revision = 0;                // +1 on every save, by whichever program saves: readers can tell that something changed
    std::string id;                  // file name without .json
    std::string name, nickname, player;
    std::string age = "adult";       // young | adult | old
    Ref kin, profession;
    std::string school;              // a mage's school of magic
    Scores attr;                     // by attribute id; one a sheet does not give counts as 10
    int hp = 10, wp = 10;            // current values
    bool roundRest = false, stretchRest = false;   // the rests taken this shift: the Master clears them when the next shift begins
    int deathSuccesses = 0, deathFailures = 0;   // the death rolls at 0 HP: three of either ends it (see the sheet)
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
    std::map<std::string, Review> reviews;       // by issue key ("encumbrance", "attr:strength"...)
    std::string createdAt, updatedAt;            // ISO 8601, UTC

    std::string displayName() const;             // "Aria "Silvervoice""
    // The sheet as plain text (the creator's preview): the attributes' short forms come from `names` (id -> "STR"/"FUE"), the damage
    // bonus and the base chance from the attributes' tables.
    std::string summary(const AttributeSet& rules, const std::function<std::string(const std::string&)>& abbr, const std::string& ageName) const;
    std::string toJson() const;                  // pretty-printed, what is written to disk
    static bool fromJson(const std::string& text, Character& out, std::string* error);
    // Brings a character file (or a set of changes meant for one) written before attributes had ids up to date: the codes in `attributes`,
    // in each skill's `attribute` and in `reviews` become ids. A whole file also becomes format 2.
    static void migrateJson(json& doc);
};

// ------------------------------------------------------------------------------------------ rules
// Numbers from the Dragonbane Rulebook, chapter 2 (pages 28-29 of the printed book).

// The three that read a table of the attributes (data/system/lookups.yaml: "base-chance", "movement-modifier", "damage-bonus").
int baseChance(const AttributeSet& rules, int score);           // a skill's base chance: 1-5:3, 6-8:4, 9-12:5, 13-15:6, 16+:7
int movementModifier(const AttributeSet& rules, int agility);   // 1-6:-4, 7-9:-2, 10-12:0, 13-15:+2, 16+:+4
std::string damageBonus(const AttributeSet& rules, int score);  // "" up to 12, "D4" 13-16, "D6" 17+ (strength or agility)
int encumbranceLimit(const Character& c);             // half STR rounded up, +2 with a backpack (in the inventory or among the tiny items)
double itemWeight(const Item& it);                    // what one of them weighs: its own weight, else 1 (food rations 1/4)
double weightFromText(const std::string& text);       // a catalog "Weight" ("1", "1/4", "—" = 0); negative if it says nothing usable
int carriedItems(const Character& c);                 // encumbrance of the inventory: each row weighs its weight x count, rounded up
int maxHp(const Character& c);                        // CON (+ bonus)
int maxWp(const Character& c);                        // WIL (+ bonus)


// Rolled attributes (one score per attribute of `rules`, in its order) + age adjustment, each clamped to 1..18.
Scores applyAge(const AttributeSet& rules, const std::vector<int>& rolled, const AgeDef& age);

// Effective level of a skill: the recorded level, or the base chance of its attribute.
int skillLevel(const AttributeSet& rules, const Character& c, const SkillEntry* entry, const std::string& attribute);

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
