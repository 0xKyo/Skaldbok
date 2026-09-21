// Plain data shared by the book database (db.h) and the content packs (content.h). No behaviour, no UI.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gm {

// What an entry is; also the `kind` of search hits.
enum class Kind { Monster, Spell, Ability, Skill, Kin, Profession, Table, Weapon, Armor, Gear };
constexpr int kKindCount = 10;

const char* kindLabel(Kind k);                       // "Creature", "Spell"...
const char* kindKey(Kind k);                         // "monster", "spell"... stable name used in saved files
bool kindFromKey(const std::string& s, Kind& out);

// Tables that come from a content pack (not from the book database) get ids from here up.
constexpr int kPackTableBase = 1000000;

// A book of the database: rulebook | bestiary | adventure.
struct Source {
    int id = 0;
    std::string key;
    std::string title;
    std::string file;     // relative to the project root, e.g. References/Dragonbane_Rulebook.pdf
};

// One line of a list on the left side.
struct ListItem {
    int id = 0;
    Kind kind = Kind::Monster;
    std::string name;
    std::string sub;       // small grey text: category, school, ...
    int sourceId = 0;
    int page = 0;          // physical page in its PDF
};

// Where the original lives: enough to open the PDF at the right page.
struct PageRef {
    int sourceId = 0;
    int page = 0;          // physical
    int printed = 0;       // as printed on the page (0 = unknown)
    bool valid() const { return sourceId != 0 && page > 0; }
};

struct Field {
    std::string label, value;
};

// A reference card: spells, abilities, skills, kin, professions, weapons, armor, gear.
struct Entry {
    Kind kind = Kind::Spell;
    int id = 0;                        // handle within its kind, valid while the content stays loaded
    std::string key;                   // stable: "<pack>/<kind>/<id>", what characters and saved files refer to
    int sourceId = 0;
    std::string title, subtitle;
    std::vector<Field> fields;
    std::string body;
    PageRef ref;                       // a book page that can be opened (core books only)
    std::string pageNote;              // "p.12" for sources without a PDF (homebrew)
    // typed data for the character creator ("attribute", "school", "movement"...)
    std::map<std::string, std::string> props;
    std::map<std::string, std::vector<std::string>> lists;

    std::string prop(const std::string& name) const {
        auto it = props.find(name);
        return it == props.end() ? std::string() : it->second;
    }
    const std::vector<std::string>& list(const std::string& name) const {
        static const std::vector<std::string> none;
        auto it = lists.find(name);
        return it == lists.end() ? none : it->second;
    }
};

struct StatBlock {
    std::string variant;               // "Scout"; empty when the creature has a single block
    std::vector<Field> fields;         // every labelled field as printed
    PageRef ref;
};

struct Attack {
    int rollMin = 0, rollMax = 0;
    std::string rollText, name, text;
    PageRef ref;
};

struct NamedText {
    std::string name, kind, text;      // kind: ability | pc_ability
};

struct TableRef {
    int id = 0;
    std::string title;
};

struct Monster {
    int id = 0;
    std::string key;
    int sourceId = 0;
    std::string name, kind, category, description, quote, randomEncounter, adventureSeed, statsRef;
    std::string attackDice;            // "D6", "D4"
    std::string image;                 // absolute path; empty if none
    std::string pageNote;
    PageRef ref, imageRef;
    std::vector<StatBlock> blocks;
    std::vector<Attack> attacks;
    std::vector<NamedText> abilities;
    std::vector<TableRef> tables;
};

struct TableRow {
    int rollMin = 0, rollMax = 0;
    std::string rollText;
    std::vector<std::string> cells;
};

struct DataTable {
    int id = 0;
    std::string key;
    std::string role;                  // pack tables: "weakness", "memento"... what the character creator rolls on
    bool browse = true;                // shown in the Tables list
    int sourceId = 0;
    std::string title, dice;           // dice: "D6", "D20", "" for plain tables
    std::vector<std::string> columns;
    std::vector<TableRow> rows;
    PageRef ref;
    std::string pageNote;
    int dieSides() const;              // 6 for "D6", 0 if none
    int rowForRoll(int roll) const;    // index of the row that covers `roll`, -1 if none
};

// A piece of a book that belongs to no other category (see Database::listRules); parents come before their children.
struct RuleNode {
    int id = 0;
    int parent = 0;                    // 0 = top level
    int level = 1;
    std::string title, body;
    PageRef ref;
};

struct Hit {
    Kind kind = Kind::Table;
    int id = 0;
    std::string title, snippet;
    int sourceId = 0;
    int page = 0;
    bool exact = false;                // the title is exactly what was typed
};

}  // namespace gm
