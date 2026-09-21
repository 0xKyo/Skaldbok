// Tiny RAII wrapper around a prepared SQLite statement, shared by the database and the search index.
#pragma once

#include <string>

#include <sqlite3.h>

namespace gm {

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) s_ = nullptr;
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return s_ != nullptr; }
    Stmt& bind(int i, int v) { sqlite3_bind_int(s_, i, v); return *this; }
    Stmt& bind(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    bool step() { return s_ && sqlite3_step(s_) == SQLITE_ROW; }
    bool run() { return s_ && sqlite3_step(s_) == SQLITE_DONE; }
    void reset() { if (s_) { sqlite3_reset(s_); sqlite3_clear_bindings(s_); } }
    int i(int c) const { return sqlite3_column_int(s_, c); }
    std::string s(int c) const {
        const unsigned char* t = sqlite3_column_text(s_, c);
        return t ? std::string(reinterpret_cast<const char*>(t), static_cast<size_t>(sqlite3_column_bytes(s_, c)))
                 : std::string();
    }

private:
    sqlite3_stmt* s_ = nullptr;
};

}  // namespace gm
