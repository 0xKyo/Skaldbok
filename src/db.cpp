#include "db.h"

#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <sqlite3.h>

#include "fts.h"
#include "sql.h"

namespace gm {
namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');    // same as SQLite's lower(): ASCII only
    return s;
}

PageRef ref(int source, int page, int printed) {
    PageRef r;
    r.sourceId = source;
    r.page = page;
    r.printed = printed;
    return r;
}

}  // namespace

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

bool Database::open(const std::string& path, std::string* error) {
    // immutable: the file is never written, so SQLite can skip locking and journal handling entirely.
    const std::string uri = "file:" + path + "?immutable=1&mode=ro";
    std::string escaped;
    for (char c : uri) {
        if (c == '\\') escaped += '/';
        else if (c == ' ') escaped += "%20";
        else escaped += c;
    }
    if (sqlite3_open_v2(escaped.c_str(), &db_, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        if (error) *error = db_ ? sqlite3_errmsg(db_) : "cannot open database";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_exec(db_, "PRAGMA mmap_size=67108864; PRAGMA cache_size=-4096;", nullptr, nullptr, nullptr);
    Stmt q(db_, "SELECT id, key, title, file FROM sources ORDER BY id");
    if (!q.ok()) {
        if (error) *error = std::string("not a Dragonbane database: ") + sqlite3_errmsg(db_);
        return false;
    }
    while (q.step()) sources_.push_back({q.i(0), q.s(1), q.s(2), q.s(3)});
    return true;
}

const Source* Database::source(int id) const {
    for (const auto& s : sources_)
        if (s.id == id) return &s;
    return nullptr;
}

int Database::sourceIdByKey(const std::string& key) const {
    for (const auto& s : sources_)
        if (s.key == key) return s.id;
    return 0;
}

int64_t Database::count(const std::string& table) {
    Stmt q(db_, ("SELECT COUNT(*) FROM " + table).c_str());
    return q.step() ? q.i(0) : -1;
}

// ------------------------------------------------------------------------------------------ lists

std::vector<ListItem> Database::listTables() {
    std::vector<ListItem> out;
    Stmt q(db_, "SELECT t.id, t.title, t.source_id, COALESCE(t.dice,''), t.page, "
                "(SELECT COUNT(*) FROM game_table_rows r WHERE r.table_id=t.id), COALESCE(t.printed_page,0) "
                "FROM game_tables t "
                "WHERE t.columns <> '[\"ATTACK\"]' "          // attack tables live on their creature's page
                "ORDER BY t.source_id, t.page, t.id");
    while (q.step()) {
        ListItem it;
        it.id = q.i(0);
        it.kind = Kind::Table;
        it.name = q.s(1);
        it.sub = (q.s(3).empty() ? "" : q.s(3) + " · ") + std::to_string(q.i(5)) + " rows";
        if (q.i(6) > 0) it.sub += " · p." + std::to_string(q.i(6));
        it.sourceId = q.i(2);
        it.page = q.i(4);
        out.push_back(std::move(it));
    }
    return out;
}

std::vector<RuleNode> Database::listRules() {
    // Sections that already became an entry of another category (the entry's own text is its section body).
    std::unordered_set<int> covered;
    Stmt c(db_, "SELECT section_id FROM skills UNION SELECT section_id FROM abilities UNION SELECT section_id FROM kin "
                "UNION SELECT section_id FROM professions UNION SELECT section_id FROM spells UNION SELECT section_id FROM game_tables");
    while (c.step()) covered.insert(c.i(0));
    std::set<std::pair<int, std::string>> creatures;               // a book section named like a creature is that creature
    Stmt m(db_, "SELECT source_id, lower(name) FROM monsters");
    while (m.step()) creatures.emplace(m.i(0), m.s(1));

    struct Row {
        RuleNode node;
        bool out = false, hasText = false;
    };
    std::vector<Row> rows;
    std::unordered_map<int, size_t> at;
    Stmt q(db_, "SELECT s.id, s.source_id, COALESCE(s.parent_id,0), s.level, s.kind, s.title, s.page_start, "
                "COALESCE(s.printed_start,0), s.body FROM sections s JOIN sources o ON o.id = s.source_id "
                "WHERE o.key <> 'adventure' ORDER BY s.source_id, s.ord");
    while (q.step()) {
        Row r;
        r.node.id = q.i(0);
        r.node.parent = q.i(2);
        r.node.level = q.i(3);
        r.node.title = q.s(5);
        r.node.body = q.s(8);
        r.node.ref = ref(q.i(1), q.i(6), q.i(7));
        const std::string kind = q.s(4);
        const bool own = covered.count(r.node.id) || kind == "table" || kind == "monster" || kind == "npc" || kind == "statblock" ||
                         kind == "ability" || kind == "pc_ability" || kind == "map" ||
                         creatures.count({q.i(1), lower(r.node.title)}) || (r.node.level == 1 && (r.node.title == "Contents" || r.node.title == "Index"));
        const auto parent = at.find(r.node.parent);
        r.out = own || (parent != at.end() && rows[parent->second].out);          // everything inside an excluded section goes too
        r.hasText = !r.node.body.empty();
        at[r.node.id] = rows.size();
        rows.push_back(std::move(r));
    }
    // A heading stays only if it has text or something with text below it.
    for (size_t i = rows.size(); i-- > 0;) {
        Row& r = rows[i];
        if (r.out || !r.hasText) continue;
        if (const auto p = at.find(r.node.parent); p != at.end()) rows[p->second].hasText = true;
    }
    std::vector<RuleNode> out;
    for (Row& r : rows)
        if (!r.out && r.hasText) out.push_back(std::move(r.node));
    return out;
}

// ---------------------------------------------------------------------------------------- details

bool Database::table(int id, DataTable& t) {
    t = DataTable{};
    {
        Stmt q(db_, "SELECT id, source_id, title, COALESCE(dice,''), columns, page, COALESCE(printed_page,0) "
                    "FROM game_tables WHERE id=?");
        q.bind(1, id);
        if (!q.step()) return false;
        t.id = q.i(0);
        t.sourceId = q.i(1);
        t.title = q.s(2);
        t.dice = q.s(3);
        t.ref = ref(t.sourceId, q.i(5), q.i(6));
        Stmt c(db_, "SELECT value FROM json_each(?) ORDER BY key");
        c.bind(1, q.s(4));
        while (c.step()) t.columns.push_back(c.s(0));
    }
    Stmt q(db_, "SELECT r.ord, COALESCE(r.roll_min,0), COALESCE(r.roll_max,0), COALESCE(r.roll_text,''), j.key, j.value "
                "FROM game_table_rows r, json_each(r.cells) j WHERE r.table_id=? ORDER BY r.ord, j.key");
    q.bind(1, id);
    int lastOrd = -1;
    while (q.step()) {
        if (q.i(0) != lastOrd) {
            lastOrd = q.i(0);
            TableRow r;
            r.rollMin = q.i(1);
            r.rollMax = q.i(2);
            r.rollText = q.s(3);
            t.rows.push_back(std::move(r));
        }
        t.rows.back().cells.push_back(q.s(5));
    }
    return true;
}

int Database::pageCount(int sourceId) {
    Stmt q(db_, "SELECT COUNT(*) FROM pages WHERE source_id=?");
    q.bind(1, sourceId);
    return q.step() ? q.i(0) : 0;
}

int Database::printedPage(int sourceId, int page) {
    Stmt q(db_, "SELECT COALESCE(printed_page,0) FROM pages WHERE source_id=? AND page=?");
    q.bind(1, sourceId).bind(2, page);
    return q.step() ? q.i(0) : 0;
}

// ------------------------------------------------------------------------------------------ search

std::vector<Hit> Database::search(const std::string& userText, int limit) {
    const std::string match = ftsMatch(userText);
    std::vector<Hit> out;
    if (match.empty()) return out;

    // Only the books' tables are searched here; creatures, spells and the rest are indexed by the content store,
    // which also covers homebrew. An entry whose title is exactly what was typed comes first.
    Stmt q(db_, "SELECT kind, ref_id, title, snippet(search_index, 3, '\x01', '\x02', ' … ', 14), source, page, "
                "lower(title) = lower(?3) FROM search_index WHERE search_index MATCH ?1 AND kind = 'table' "
                "ORDER BY 7 DESC, bm25(search_index, 0.0, 0.0, 12.0, 1.0, 0.0, 0.0) LIMIT ?2");
    if (!q.ok()) return out;
    q.bind(1, match).bind(2, limit).bind(3, trimmed(userText));
    while (q.step()) {
        Hit h;
        h.kind = Kind::Table;
        h.id = q.i(1);
        h.title = q.s(2);
        h.snippet = q.s(3);
        h.sourceId = sourceIdByKey(q.s(4));
        h.page = q.i(5);
        h.exact = q.i(6) != 0;
        out.push_back(std::move(h));
    }
    return out;
}

}  // namespace gm
