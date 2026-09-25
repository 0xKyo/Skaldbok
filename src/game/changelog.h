// What a player changed on their own sheet from the web, kept per character so the GM can see it, be told about it as it happens,
// and undo it. One file per character (changes/<character id>.json). No UI in here.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "parsing/jsonutil.h"

namespace gm {

struct ChangeEntry {
    std::string id;                  // sorts by time
    std::string at;                  // ISO 8601, UTC
    std::string by;                  // "player" or "gm"
    std::string summary;             // "HP 10 → 7; +Rope"
    json before, after;              // only the parts that changed, in the character file's own shape: applying `before` undoes it
    bool undone = false;
};

class ChangeLog {
public:
    static constexpr size_t kKeep = 100;

    void setPrefDir(const std::string& prefDir);                     // "<prefDir>changes/"
    void add(const std::string& characterId, ChangeEntry entry);     // fills in the id and the time
    std::vector<ChangeEntry> entries(const std::string& characterId) const;      // oldest first
    bool markUndone(const std::string& characterId, const std::string& entryId);

    // Entries that appeared (from any program) since the last call. The first call only takes note of what is there.
    std::vector<std::pair<std::string, ChangeEntry>> poll();

private:
    struct Stamp {
        long long mtime = 0;
        unsigned long long size = 0;
        bool operator==(const Stamp&) const = default;
    };
    std::string fileFor(const std::string& characterId) const { return dir_ + "/" + characterId + ".json"; }
    void save(const std::string& characterId, const std::vector<ChangeEntry>& list) const;

    std::string dir_;
    std::map<std::string, Stamp> stamps_;
    std::map<std::string, std::string> seen_;          // the newest entry id already reported, per character
    bool primed_ = false;
};

}  // namespace gm
