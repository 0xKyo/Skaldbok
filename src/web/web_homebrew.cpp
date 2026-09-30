#include "web/web_homebrew.h"

#include <algorithm>
#include <cctype>
#include <set>

#include <SDL3/SDL.h>

#include "parsing/content.h"
#include "parsing/fsutil.h"
#include "parsing/fts.h"

namespace gm {
namespace {

constexpr const char* kPackId = "custom";

bool text(const json& o, const char* key, size_t max, std::string& out, std::string& error) {
    out.clear();
    const json* v = jsonFind(o, key);
    if (!v || v->is_null()) return true;
    if (!v->is_string()) {
        error = std::string(key) + " must be text";
        return false;
    }
    out = trimmed(v->get<std::string>());
    if (out.size() > max) {
        error = std::string(key) + " is too long (" + std::to_string(max) + " letters at most)";
        return false;
    }
    return true;
}

// The creatures file as a list, whichever shape it has (a list, or an object with "creatures").
json readCreatures(const std::string& packDir) {
    json list = json::array();
    if (const auto root = jsonLoad(packDir + "/creatures.yaml")) {
        if (root->is_array()) list = *root;
        else if (const json* c = jsonFind(*root, "creatures"); c && c->is_array()) list = *c;
    }
    return list;
}

bool writeCreatures(const std::string& packDir, const json& list) {
    SDL_CreateDirectory(packDir.c_str());
    if (!fs::isFile(packDir + "/manifest.yaml")) {
        const json manifest = {{"format", 1}, {"id", kPackId}, {"name", "My Homebrew"}, {"version", "1"}, {"description", "Made on the GM page."}};
        if (!fs::writeFile(packDir + "/manifest.yaml", manifest.dump(2))) return false;
    }
    return fs::writeFile(packDir + "/creatures.yaml", json({{"format", 1}, {"creatures", list}}).dump(2));
}

json withKey(json creature) {
    creature["key"] = std::string(kPackId) + "/monster/" + creature.value("id", "");
    return creature;
}

// One creature as the form sends it, checked. `error` says what is wrong.
bool checked(const json& in, json& out, std::string& error) {
    if (!in.is_object()) {
        error = "Send the creature as an object.";
        return false;
    }
    std::string name, kind, category, quote, description, dice, seed, encounter;
    if (!text(in, "name", 80, name, error) || !text(in, "kind", 20, kind, error) || !text(in, "category", 60, category, error) || !text(in, "quote", 600, quote, error) ||
        !text(in, "description", 4000, description, error) || !text(in, "attack_dice", 4, dice, error) || !text(in, "adventure_seed", 1000, seed, error) ||
        !text(in, "random_encounter", 1000, encounter, error))
        return false;
    if (name.empty()) {
        error = "A creature needs a name.";
        return false;
    }
    kind = lowerCopy(kind);
    if (kind.empty()) kind = "monster";
    if (kind != "monster" && kind != "npc" && kind != "animal") {
        error = "kind must be monster, npc or animal";
        return false;
    }
    dice = dice.empty() ? std::string("D6") : dice;
    for (char& c : dice) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const std::set<std::string> kDice = {"D4", "D6", "D8", "D10", "D12", "D20"};
    if (!kDice.count(dice)) {
        error = "attack_dice must be D4, D6, D8, D10, D12 or D20";
        return false;
    }
    out = json::object();
    out["name"] = name;
    out["kind"] = kind;
    if (!category.empty()) out["category"] = category;
    if (!quote.empty()) out["quote"] = quote;
    if (!description.empty()) out["description"] = description;
    out["attack_dice"] = dice;

    // stat blocks: a few, each with its labelled fields in the order they were written
    json blocks = json::array();
    if (const json* sb = jsonFind(in, "statblocks"); sb && !sb->is_null()) {
        if (!sb->is_array() || sb->size() > 6) {
            error = "statblocks must be a list of at most 6";
            return false;
        }
        for (const json& b : *sb) {
            if (!b.is_object()) {
                error = "a stat block must be an object";
                return false;
            }
            std::string variant;
            if (!text(b, "variant", 40, variant, error)) return false;
            json fields = json::object();
            if (const json* f = jsonFind(b, "fields"); f && !f->is_null()) {
                if (!f->is_object() || f->size() > 30) {
                    error = "a stat block has at most 30 fields";
                    return false;
                }
                for (auto it = f->begin(); it != f->end(); ++it) {
                    const std::string label = trimmed(it.key());
                    if (label.empty()) continue;
                    if (label.size() > 40 || !(it.value().is_string() || it.value().is_number())) {
                        error = "a stat field needs a short label and a value";
                        return false;
                    }
                    const std::string value = trimmed(it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
                    if (value.size() > 200) {
                        error = "a stat value is too long";
                        return false;
                    }
                    fields[label] = value;
                }
            }
            if (fields.empty()) continue;
            json block = json::object();
            if (!variant.empty()) block["variant"] = variant;
            block["fields"] = fields;
            blocks.push_back(block);
        }
    }
    if (!blocks.empty()) out["statblocks"] = blocks;

    // the attacks table, and the special abilities: rows of a name and a text (and a roll for an attack)
    for (const char* key : {"attacks", "abilities"}) {
        const bool attack = std::string(key) == "attacks";
        const json* rows = jsonFind(in, key);
        if (!rows || rows->is_null()) continue;
        if (!rows->is_array() || rows->size() > 30) {
            error = std::string(key) + " must be a list of at most 30";
            return false;
        }
        json list = json::array();
        for (const json& r : *rows) {
            if (!r.is_object()) {
                error = std::string("an entry of ") + key + " must be an object";
                return false;
            }
            std::string rowName, rowText, roll;
            if (!text(r, "name", 80, rowName, error) || !text(r, "text", 1000, rowText, error) || (attack && !text(r, "roll", 20, roll, error))) return false;
            if (rowName.empty() && rowText.empty()) continue;
            json row = json::object();
            if (attack && !roll.empty()) row["roll"] = roll;
            row["name"] = rowName;
            row["text"] = rowText;
            list.push_back(row);
        }
        if (!list.empty()) out[key] = list;
    }
    if (!seed.empty()) out["adventure_seed"] = seed;
    if (!encounter.empty()) out["random_encounter"] = encounter;
    return true;
}

}  // namespace

std::string customPackDir(const std::string& prefsDir) { return fs::withoutTrailingSlash(prefsDir) + "/packs/" + kPackId; }

json homebrewCreatures(const std::string& packDir) {
    json list = json::array();
    for (const json& c : readCreatures(packDir))
        if (c.is_object()) list.push_back(withKey(c));
    return {{"creatures", list}};
}

HomebrewResult saveHomebrewCreature(const std::string& packDir, const std::string& id, const json& body) {
    HomebrewResult r;
    json creature;
    if (!checked(body, creature, r.error)) {
        r.status = 400;
        return r;
    }
    json list = readCreatures(packDir);
    std::set<std::string> used;
    for (const json& c : list)
        if (c.is_object()) used.insert(c.value("id", ""));
    if (id.empty()) {
        std::string base = slugOf(creature["name"].get<std::string>());
        if (base.empty()) base = "creature";
        std::string candidate = base;
        for (int n = 2; used.count(candidate); ++n) candidate = base + "-" + std::to_string(n);
        creature["id"] = candidate;
        list.push_back(creature);
        r.status = 201;
    } else {
        bool found = false;
        for (json& c : list) {
            if (!c.is_object() || c.value("id", "") != id) continue;
            creature["id"] = id;
            for (const char* keep : {"image", "image_page", "page", "stats_ref"})         // what the form has no field for stays as it was
                if (c.contains(keep)) creature[keep] = c[keep];
            c = creature;
            found = true;
        }
        if (!found) {
            r.status = 404;
            r.error = "No such creature in your homebrew.";
            return r;
        }
    }
    if (!writeCreatures(packDir, list)) {
        r.status = 500;
        r.error = "Could not save the creature.";
        return r;
    }
    r.body = withKey(creature);
    return r;
}

HomebrewResult deleteHomebrewCreature(const std::string& packDir, const std::string& id) {
    HomebrewResult r;
    json list = readCreatures(packDir), kept = json::array();
    for (const json& c : list)
        if (!(c.is_object() && c.value("id", "") == id)) kept.push_back(c);
    if (kept.size() == list.size()) {
        r.status = 404;
        r.error = "No such creature in your homebrew.";
        return r;
    }
    if (!writeCreatures(packDir, kept)) {
        r.status = 500;
        r.error = "Could not delete the creature.";
        return r;
    }
    r.body = {{"ok", true}};
    return r;
}

}  // namespace gm
