// Plain data of the content packs (content.h). No behaviour, no UI.
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

// Tables get ids from here up (every table comes from a content pack), so they never look like another kind's handle.
constexpr int kPackTableBase = 1000000;

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
    int rule = 0;                      // the rule it is shown in (RuleNode::id); 0 for tables nobody lists (browse: false)
    int dieSides() const;              // 6 for "D6", 0 if none
    int rowForRoll(int roll) const;    // index of the row that covers `roll`, -1 if none
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
    std::string image;                 // absolute path to a small illustration for the card; empty if none
    PageRef ref;                       // a book page that can be opened (core books only)
    std::string pageNote;              // "p.12" for sources without a PDF (homebrew)
    std::string editedBy;              // name of the pack that replaced this entry (see "replaces"), empty if it is as first loaded
    // typed data for the character creator ("attribute", "school", "movement"...)
    std::map<std::string, std::string> props;
    std::map<std::string, std::vector<std::string>> lists;
    // Tables that belong to this card (a kin's table of first names): shown when the card is opened. They are not in the list of tables
    // of the rules (id 0, no rule); the card's own text search covers them.
    std::vector<DataTable> tables;

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

// A rule: a piece of a book's text that belongs to no other category, or a homerule. Rules live in packs (rules.json); the
// Core opens first and holds the books' text, other packs add rules under it or replace some. Parents come before their children.
struct RuleNode {
    int id = 0;                        // handle, valid while the content stays loaded
    std::string key;                   // stable: "<pack>/rule/<id>", what other packs refer to
    int parent = 0;                    // 0 = top level
    int level = 1;
    std::string title, body;
    int sourceId = 0;                  // where it comes from (a book, or a homebrew source)
    PageRef ref;                       // a book page that can be opened (books only)
    std::string pageNote;              // "p.12" for sources without a PDF
    std::string editedBy;              // name of the pack that replaced this rule, empty if it is as first loaded
    // Extra named parts of the same page ("Mages", "Starting Scores"...): their own heading, shown after the main body and before the
    // rule's tables, instead of being a separate rule a reader has to click into. A rule can have several.
    struct Section {
        std::string title, body;
    };
    std::vector<Section> sections;
    // What the rule points to in the rest of the data, for the reader to jump to: a category of entries ("kin", "professions", "skills",
    // "abilities", "spells", "weapons", "armor", "gear", "creatures"), the key of an entry ("core/kin/human") or the key of a rule.
    std::vector<std::string> see;
    // Every other key the rule has ("step": "4", "trained_skills": 6, "attribute_mods": {"AGL": 1}...): data for the program, such as the
    // character creator. Text and numbers as written; objects and lists as their JSON.
    std::map<std::string, std::string> props;
    std::string prop(const std::string& name) const {
        const auto it = props.find(name);
        return it == props.end() ? std::string() : it->second;
    }
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
