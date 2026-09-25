// The game content the app shows: rules, creatures, spells, abilities, skills, kin, professions, equipment and tables.
//
// It lives in *content packs*: a folder of JSON files, one per type, and optionally a manifest.json (docs/HOMEBREW.md).
// The Core pack (data/packs/core: the creatures, spells, kin...; the books' generic rules and each page's intro live next to it in data/system) opens
// first and is the base the others build on; imported homebrew, homerules included, loads through the same code after it. Nothing
// here draws anything.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "game/model.h"

struct sqlite3;

namespace gm {

// Where an entry comes from: one of the books (declared with their PDF by a built-in pack), or a source declared by a homebrew pack.
struct SourceInfo {
    int id = 0;
    std::string key;         // within its pack ("rulebook", "tome")
    std::string packId;
    std::string title;       // "Dragonbane Core Rules"
    std::string label;       // short badge text: "Rulebook", "Homebrew · Tome of Wonders"
    bool book = false;       // one of the PDF books
    bool homebrew = false;
    unsigned color = 0;      // 0xRRGGBB
    std::string file;        // books: the PDF, relative to the project root (References/Dragonbane_Rulebook.pdf)
    int pages = 0;           // books: physical pages of the PDF
    std::vector<int> printedPages;   // books: the number printed on each physical page (index = page - 1), 0 = none
};

struct PackInfo {
    std::string id, name, version, author, description, dir;
    bool core = false;
    bool enabled = true;
    bool loaded = false;                 // content was read (false when disabled or broken)
    std::string error;                   // why the pack could not be used
    std::vector<std::string> warnings;   // entries that were skipped or adjusted
    std::vector<int> sourceIds;
    int counts[kKindCount] = {};
    int rules = 0;                       // rules added (rules.json)
    int total() const {
        int n = rules;
        for (int c : counts) n += c;
        return n;
    }
};

struct PackSpec {
    std::string dir;
    bool core = false;
    bool enabled = true;
};

// Reads what a pack says about itself, and nothing else (used to look at a folder before importing it and to list disabled packs). That
// is its manifest.json or, if it has none, the header at the top of its first data file that has one; without either, its id is the
// name of its folder.
bool readManifest(const std::string& dir, PackInfo& out);

// What a card's key is made of: its "id" or name, lower case, every run of other characters one "-" ("Frog People" -> "frog-people").
// A card's key is "<pack>/<kind>/<slug>" (the same slug twice in a pack: "-2", "-3"...).
std::string slugOf(const std::string& s);

// The data files a pack may have, one per type (docs/HOMEBREW.md): "rules" for rules.json, "spells" for spells.json...
const std::vector<std::string>& packDataFiles();
// A folder is a pack when it has a manifest.json or at least one of those data files.
bool looksLikePack(const std::string& dir);
// Where Core keeps what is not an item of a category (data/system: rules.json, the "intro" of each data file), given its pack folder
// (data/packs/core). Empty if it cannot be worked out.
std::string systemDirOf(const std::string& coreDir);

// A kind's own general text (its data file's top-level "intro"): shown as its page's "Intro" tab. Either a plain string
// ("intro": "...") or, like a rule, a body plus named sections and its own tables
// ("intro": {"body": "...", "sections": [{"name","body"}], "tables": [...]}).
struct Intro {
    std::string body;
    std::vector<RuleNode::Section> sections;
    std::vector<DataTable> tables;                     // card-owned (id 0), like Entry::tables
    std::string file;                                  // the JSON it was read from (where the in-app editor writes it back)
    bool empty() const { return body.empty() && sections.empty() && tables.empty(); }
};

// Where a rule's "see" points (RuleNode::see), resolved against what is loaded.
struct SeeTarget {
    enum class Type { None, Category, Entry, Rule, Section };
    Type type = Type::None;            // None: it points to nothing that is loaded
    Kind kind = Kind::Spell;           // Category: which kind of entries; Entry: the kind of the entry; Section: the kind whose intro has it
    int id = 0;                        // Entry: its handle (Host::goTo); Rule: the rule's id; Section: its index in the kind's intro
    int line = -1;                     // Rule / Section: the line of that text the keyword is on (0 = its first), -1 when not known
    int section = -1;                  // Rule: the section of the rule (RuleNode::sections) it points into, -1 for the rule as a whole
    std::string label;                 // what to show: "Kin (6)", "Human", "Age"
};

class ContentStore {
public:
    ContentStore() = default;
    ~ContentStore();
    ContentStore(const ContentStore&) = delete;
    ContentStore& operator=(const ContentStore&) = delete;

    // (Re)reads every pack. Never fails as a whole: a broken pack is reported in packs() and skipped.
    void load(const std::vector<PackSpec>& specs);

    const std::vector<PackInfo>& packs() const { return packs_; }
    const PackInfo* pack(const std::string& id) const;
    const std::vector<SourceInfo>& sources() const { return sources_; }
    const SourceInfo* source(int id) const;
    int sourceId(const std::string& packId, const std::string& key) const;
    int bookId(const std::string& key) const;                                // a book by its key ("rulebook"); 0 if it is not loaded
    int pageCount(int sourceId) const;                                       // physical pages of a book's PDF
    int printedPage(int sourceId, int page) const;                           // 0 when the page carries no number

    // Rules, in reading order (parents before their children): what Core has, plus what other packs added or replaced.
    const std::vector<RuleNode>& rules() const { return rules_; }
    const RuleNode* rule(int id) const;
    int ruleByKey(const std::string& key) const;                             // 0 if it is not loaded
    // The tables shown inside a rule (ids for packTable()), in the order they are written. A table written in a rule's "tables" belongs
    // to it; a browsable table of a pack's tables.json belongs to a rule of its own, "Tables · <pack>", added at the end of the tree.
    const std::vector<int>& tablesOfRule(int ruleId) const;
    SeeTarget seeTarget(const std::string& ref) const;

    // cards: spells, abilities, skills, kin, professions, weapons, armor, gear
    const std::vector<Entry>& entries(Kind k) const;
    const Entry* entry(Kind k, int id) const;
    const Entry* findByName(Kind k, const std::string& name) const;         // case-insensitive, first match
    // What a link in a text points to: a full "see" reference (seeTarget) or a keyword, tried as a category ("spells", "Spells"), then
    // the name of an entry of any kind, then the title of a rule, then the title of a section of an intro ("Pushing Your Roll", or
    // "skills/Pushing Your Roll" to say whose). Type::None if it is none of those.
    SeeTarget resolveLink(const std::string& ref) const;
    // Every keyword a text marks with {{key: word}} (or {{key: shown text | word}}): the word, lower case, and where it is. A link to a
    // keyword ("[[word]]") goes there, from any text.
    const std::map<std::string, SeeTarget>& keywords() const { return keywords_; }
    const Intro& introOf(Kind k) const;

    const std::vector<Monster>& monsters() const { return monsters_; }
    const Monster* monster(int id) const;
    std::vector<ListItem> listMonsters() const;
    std::vector<ListItem> creatureVersions(const std::string& name, int exceptId) const;
    std::vector<ListItem> creaturesOnPrintedPage(int sourceId, int printedPage) const;

    // every table of every pack (the books' tables live in Core, inside their sections)
    const std::vector<DataTable>& packTables() const { return tables_; }
    const DataTable* packTable(int id) const;
    // A table by its title (case-insensitive), wherever it is: the tables of the rules first, then those of an intro, then those of a
    // card. What a "{{table: Name}}" line in a text shows. Null if there is none.
    const DataTable* tableByName(const std::string& name) const;
    const DataTable* tableByRole(const std::string& role) const;             // first enabled pack's table with that role
    std::vector<const DataTable*> tablesByRole(const std::string& role) const;

    // stable keys, for files that outlive a session (recents, encounter, characters)
    std::string keyOf(Kind k, int id) const;
    int idByKey(Kind k, const std::string& key) const;                       // 0 if it is not loaded
    std::string titleOf(Kind k, int id) const;

    // full-text search over everything a pack provides; `only` restricts it to those kinds
    std::vector<Hit> search(const std::string& userText, int limit = 60, const std::vector<Kind>& only = {}) const;

    int count(Kind k) const;

private:
    struct Loader;
    friend struct Loader;

    void clear();
    void buildIndex();
    void resolveLinks();
    void extractKeywords();
    void placeTables();
    void checkRuleLinks();

    std::vector<PackInfo> packs_;
    std::vector<SourceInfo> sources_;
    std::vector<Entry> entries_[kKindCount];
    Intro intros_[kKindCount];
    std::map<std::string, SeeTarget> keywords_;      // "boon" -> the text that has {{key: boon}} (see extractKeywords)
    std::vector<Monster> monsters_;
    std::vector<DataTable> tables_;
    std::vector<RuleNode> rules_;
    std::map<std::string, int> byKey_[kKindCount];
    std::map<std::string, int> ruleByKey_;
    std::map<int, std::vector<int>> tablesOfRule_;
    mutable sqlite3* index_ = nullptr;
};

}  // namespace gm
