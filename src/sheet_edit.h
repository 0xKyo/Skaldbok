// Editing a character sheet from more than one place at once: the player on the web, the GM in the app.
//
// Everything is field by field. A change is a "set": the parts of the character file that differ, in the file's own shape (arrays and
// scalars replace, `attributes`, `coins` and `reviews` merge key by key). Applying a set to the file as it is on disk, instead of
// writing a whole copy back, is what lets two people edit the same sheet without one erasing the other's change.
//
// The rules are not enforced by refusing: a player may do what the book does not allow, and the sheet says so (issues). The GM then
// approves it (it becomes normal) or rejects it (it stays flagged until the player fixes it by hand).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "changelog.h"
#include "character.h"
#include "jsonutil.h"

namespace gm::sheet {

// What a player may change from the web: everything on the sheet except who the character is (kin, profession, school) and the
// bookkeeping (id, revision, lock, reviews, dates), which come from the creator and the GM.
bool playerMayEdit(std::string_view key);

struct Applied {
    bool ok = true;
    std::string error;                    // what was wrong, for the player ("hp must be a whole number")
    std::vector<std::string> keys;        // the top-level keys whose value actually changed
};
// Checks the set's types and sizes and applies it to `doc`. With asPlayer, only playerMayEdit keys are accepted.
Applied applySet(json& doc, const json& set, bool asPlayer);
// The set that turns `before` into `after` (dates, ids and the revision are left out).
json diff(const json& before, const json& after);
// Applies a set without checking it (the GM's own edits, undo). A null inside `reviews` removes that review.
void merge(json& doc, const json& set);
// "HP 10 → 7; +Rope" for the parts named in the two sets (as stored in ChangeEntry).
std::string describe(const json& before, const json& after);

// ---- rules

struct Issue {
    std::string key;                      // "hp", "attr:STR", "skill:Axes", "encumbrance", "hpmax"...
    std::string message;
    std::string value;                    // what is wrong, so a ruling applies to exactly this and not to a worse case later
};
struct OpenIssue : Issue {
    std::string status;                   // pending | rejected
};

std::vector<Issue> rulesIssues(const Character& c);
std::vector<OpenIssue> openIssues(const Character& c);      // the issues nobody approved
void prune(Character& c);                                    // rulings that no longer apply are forgotten
bool review(Character& c, const std::string& key, bool approve);

// ---- editing a character file

struct EditResult {
    bool ok = false;
    int status = 200;                     // HTTP-like: 400 invalid, 404 no such character, 423 locked
    std::string error;
    Character character;                  // the sheet after the edit
    std::vector<std::string> keys;
};
// Applies `set` to the character file at `path` and writes it back atomically (revision + 1). The change is logged with `by` when
// there is a log. A locked sheet refuses a player.
EditResult editFile(const std::string& path, const std::string& characterId, const json& set, bool asPlayer, ChangeLog* log);

}  // namespace gm::sheet
