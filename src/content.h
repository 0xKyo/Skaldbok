// The game content the app shows: creatures, spells, abilities, skills, kin, professions, equipment and tables.
//
// It lives in *content packs*: a folder with a manifest.json and one JSON file per type (docs/HOMEBREW.md).
// The Core pack (data/packs/core, made by tools/export_packs.py) and any imported homebrew load through the
// same code. Nothing here draws anything.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "db.h"
#include "model.h"

struct sqlite3;

namespace gm {

// Where an entry comes from: one of the Core books, or a source declared by a homebrew pack.
struct SourceInfo {
    int id = 0;
    std::string key;         // within its pack ("rulebook", "tome")
    std::string packId;
    std::string title;       // "Dragonbane Core Rules"
    std::string label;       // short badge text: "Rulebook", "Homebrew · Tome of Wonders"
    bool book = false;       // one of the database's PDF books
    bool homebrew = false;
    unsigned color = 0;      // 0xRRGGBB
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
    int total() const {
        int n = 0;
        for (int c : counts) n += c;
        return n;
    }
};

struct PackSpec {
    std::string dir;
    bool core = false;
    bool enabled = true;
};

// Reads a pack's manifest only (used to look at a folder before importing it and to list disabled packs).
bool readManifest(const std::string& dir, PackInfo& out);

class ContentStore {
public:
    ContentStore() = default;
    ~ContentStore();
    ContentStore(const ContentStore&) = delete;
    ContentStore& operator=(const ContentStore&) = delete;

    // (Re)reads every pack. Never fails as a whole: a broken pack is reported in packs() and skipped.
    void load(Database& db, const std::vector<PackSpec>& specs);

    const std::vector<PackInfo>& packs() const { return packs_; }
    const PackInfo* pack(const std::string& id) const;
    const std::vector<SourceInfo>& sources() const { return sources_; }
    const SourceInfo* source(int id) const;
    int sourceId(const std::string& packId, const std::string& key) const;

    // cards: spells, abilities, skills, kin, professions, weapons, armor, gear
    const std::vector<Entry>& entries(Kind k) const;
    const Entry* entry(Kind k, int id) const;
    const Entry* findByName(Kind k, const std::string& name) const;         // case-insensitive, first match

    const std::vector<Monster>& monsters() const { return monsters_; }
    const Monster* monster(int id) const;
    std::vector<ListItem> listMonsters() const;
    std::vector<ListItem> creatureVersions(const std::string& name, int exceptId) const;
    std::vector<ListItem> creaturesOnPrintedPage(int sourceId, int printedPage) const;

    // tables that come from packs (the book database keeps its own)
    const std::vector<DataTable>& packTables() const { return tables_; }
    const DataTable* packTable(int id) const;
    const DataTable* tableByRole(const std::string& role) const;             // first enabled pack's table with that role
    std::vector<const DataTable*> tablesByRole(const std::string& role) const;

    // stable keys, for files that outlive a session (recents, encounter, characters)
    std::string keyOf(Kind k, int id) const;                                 // "#12" for book tables
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
    void resolveLinks(Database& db);

    std::vector<PackInfo> packs_;
    std::vector<SourceInfo> sources_;
    std::vector<Entry> entries_[kKindCount];
    std::vector<Monster> monsters_;
    std::vector<DataTable> tables_;
    std::map<std::string, int> byKey_[kKindCount];
    mutable sqlite3* index_ = nullptr;
};

// Search results from several indexes, best first: exact title matches, then named entries, then book tables.
std::vector<Hit> mergeHits(std::vector<Hit> content, std::vector<Hit> book, size_t limit);

}  // namespace gm
