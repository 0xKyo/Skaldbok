// The attributes of the game system and the numeric tables the rules read from them (docs/HOMEBREW.md, "Attributes").
//
// data/system/attributes.yaml says which attributes exist (an id each: "strength", never "STR"); data/system/lookups.yaml holds the
// tables that turn a score into a number (base chance, movement modifier, damage bonus). Neither has any words in it: names, short
// forms and descriptions are texts, found by the keys the files write (`name: LOC_ATTRIBUTE_STRENGTH`) in i18n/<lang>.csv.
// What the program stores (characters, skills, professions) always uses the id.
#pragma once

#include <string>
#include <vector>

#include "parsing/jsonutil.h"

namespace gm {

struct AttributeDef {
    std::string id;                          // "strength": what everything else refers to
    std::vector<std::string> aliases;        // codes older files and packs used ("STR"): understood when read, never written
    std::string nameKey, abbrKey, descriptionKey;      // keys of its texts ("LOC_ATTRIBUTE_STRENGTH", "..._ABBR", "..._DESC")
};

// One row of a lookup: scores from..to (either end may be open) give `value`.
struct LookupRow {
    bool hasFrom = false, hasTo = false;
    int from = 0, to = 0;
    std::string value;
    std::string labelKey;                    // the text shown for this row's range; "" = "from–to"
    bool covers(int score) const { return (!hasFrom || score >= from) && (!hasTo || score <= to); }
};

struct Lookup {
    std::string id;                          // "base-chance", "movement-modifier", "damage-bonus"
    std::vector<std::string> attributes;     // the attributes it is for; none = any
    std::string fallback;                    // the value of a score no row covers
    std::string format;                      // how a value is shown: "number" (3), "signed" (+2, –4), "bonus" (+D4, — for none)
    std::vector<LookupRow> rows;
    std::string titleKey, rangeKey, valueKey;      // the table's title and the headers of its two columns
};

class AttributeSet {
public:
    // Adds what a file says (`attributes:` / `lookups:` lists). A thing with an id already there replaces it in place; a new one is
    // added at the end. Problems are listed in `warnings`.
    void addAttributes(const json& list, std::vector<std::string>& warnings);
    void addLookups(const json& list, std::vector<std::string>& warnings);

    const std::vector<AttributeDef>& attributes() const { return attributes_; }
    const std::vector<Lookup>& lookups() const { return lookups_; }
    bool empty() const { return attributes_.empty(); }

    // The position of an attribute (by id, or by one of its old codes; case does not matter), -1 if there is none.
    int indexOf(const std::string& idOrAlias) const;
    // Its id, or "" if there is no such attribute.
    std::string idOf(const std::string& idOrAlias) const;
    const AttributeDef* find(const std::string& idOrAlias) const;
    const Lookup* lookup(const std::string& id) const;

    // The value a lookup gives for a score ("" when there is no such lookup or no value), and as a number.
    std::string value(const std::string& lookupId, int score) const;
    int number(const std::string& lookupId, int score, int otherwise = 0) const;

private:
    std::vector<AttributeDef> attributes_;
    std::vector<Lookup> lookups_;
};

}  // namespace gm
