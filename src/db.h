// Read-only access to data/dragonbane.db: the books themselves (page text, headings, tables, full-text index).
// Game content (creatures, spells, kin...) is not read from here any more; see content.h.
#pragma once

#include <string>
#include <vector>

#include "model.h"

struct sqlite3;

namespace gm {

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& path, std::string* error);

    const std::vector<Source>& sources() const { return sources_; }
    const Source* source(int id) const;
    int sourceIdByKey(const std::string& key) const;

    // lists
    std::vector<ListItem> listTables();

    // The books' text that is not already a creature, spell, skill, kin, table... (the adventure is left out), in book order.
    std::vector<RuleNode> listRules();

    // details
    bool table(int id, DataTable& out);

    int pageCount(int sourceId);
    int printedPage(int sourceId, int page);     // 0 when the page carries no number

    // full-text search over the books' tables
    std::vector<Hit> search(const std::string& userText, int limit = 60);

    // small facts for the status line / tests
    int64_t count(const std::string& table);

private:
    sqlite3* db_ = nullptr;
    std::vector<Source> sources_;
};

}  // namespace gm
