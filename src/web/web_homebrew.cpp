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

constexpr const char* kDefaultName = "Homebrew";

std::string baseName(const std::string& dir) {
    const size_t slash = dir.find_last_of('/');
    return slash == std::string::npos ? dir : dir.substr(slash + 1);
}

// <prefs>/packs/<id> -> <prefs>
std::string prefsOf(const std::string& packDir) {
    std::string d = packDir.substr(0, packDir.find_last_of('/'));                      // .../packs
    return d.substr(0, d.find_last_of('/'));
}

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
        const json manifest = {{"format", 1}, {"id", baseName(packDir)}, {"name", homebrewPackName(prefsOf(packDir))}, {"version", "1"}, {"description", "Made on the GM page."}};
        if (!fs::writeFile(packDir + "/manifest.yaml", manifest.dump(2))) return false;
    }
    return fs::writeFile(packDir + "/creatures.yaml", json({{"format", 1}, {"creatures", list}}).dump(2));
}

json withKey(json creature, const std::string& packDir) {
    creature["key"] = baseName(packDir) + "/monster/" + creature.value("id", "");
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

std::string homebrewPackName(const std::string& prefsDir) {
    if (const auto root = jsonLoad(fs::withoutTrailingSlash(prefsDir) + "/homebrew.yaml"))
        if (root->is_object()) {
            const std::string name = trimmed(jsonStr(*root, "name"));
            if (!name.empty()) return name;
        }
    return kDefaultName;
}

std::string customPackDir(const std::string& prefsDir) {
    std::string id = slugOf(homebrewPackName(prefsDir));
    if (id.empty() || !fs::safeId(id)) id = "homebrew";
    return fs::withoutTrailingSlash(prefsDir) + "/packs/" + id;
}

HomebrewResult setHomebrewPackName(const std::string& prefsDir, const std::string& name) {
    HomebrewResult r;
    const std::string clean = trimmed(name);
    if (clean.empty() || clean.size() > 40 || slugOf(clean).empty()) {
        r.status = 400;
        r.error = "Give the pack a name (up to 40 letters, with at least one letter or digit).";
        return r;
    }
    if (!fs::writeFile(fs::withoutTrailingSlash(prefsDir) + "/homebrew.yaml", json({{"name", clean}}).dump(2))) {
        r.status = 500;
        r.error = "Could not save the name.";
        return r;
    }
    r.body = {{"id", baseName(customPackDir(prefsDir))}, {"name", clean}};
    return r;
}

json homebrewCreatures(const std::string& packDir) {
    json list = json::array();
    for (const json& c : readCreatures(packDir))
        if (c.is_object()) list.push_back(withKey(c, packDir));
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
    r.body = withKey(creature, packDir);
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


// The packs one can look into: the GM's own (the only one that can be changed), Core and any other the user has.
json homebrewPacks(const std::string& prefsDir, const std::string& coreDir) {
    const auto nameOf = [](const std::string& dir, const std::string& fallback) {
        if (const auto m = jsonLoad(dir + "/manifest.yaml"); m && m->is_object() && !jsonStr(*m, "name").empty()) return jsonStr(*m, "name");
        return fallback;
    };
    const std::string own = baseName(customPackDir(prefsDir));
    json list = json::array();
    list.push_back({{"id", own}, {"name", homebrewPackName(prefsDir)}, {"editable", true}});
    list.push_back({{"id", "core"}, {"name", nameOf(coreDir, "Core")}, {"editable", false}});
    std::vector<std::string> folders = fs::listDir(fs::withoutTrailingSlash(prefsDir) + "/packs");
    std::sort(folders.begin(), folders.end());
    for (const std::string& f : folders) {
        if (f == own || f == "core" || !fs::safeId(f) || !fs::isFile(fs::withoutTrailingSlash(prefsDir) + "/packs/" + f + "/manifest.yaml")) continue;
        list.push_back({{"id", f}, {"name", nameOf(fs::withoutTrailingSlash(prefsDir) + "/packs/" + f, f)}, {"editable", false}});
    }
    return {{"packs", list}};
}

// Where the files of a pack are: Core's folder, or a folder of the user's packs. Empty when there is no such pack.
std::string homebrewPackDir(const std::string& prefsDir, const std::string& coreDir, const std::string& id) {
    if (id == "core") return coreDir;
    if (!fs::safeId(id)) return std::string();
    const std::string dir = fs::withoutTrailingSlash(prefsDir) + "/packs/" + id;
    return fs::isFile(dir + "/manifest.yaml") ? dir : std::string();
}

// The entry of a pack with that name, as stored (to start a house rule from it); null when there is none.
json homebrewOriginal(const std::string& dir, const std::string& coreDir, const std::string& section, const std::string& name) {
    const json all = homebrewEntries(dir, coreDir, section);
    if (all.is_null()) return nullptr;
    for (const json& e : all["entries"])
        if (e.is_object() && lowerCopy(e.value("name", "")) == lowerCopy(name)) return e;
    return nullptr;
}

}  // namespace gm

// ------------------------------------------------------------------------------------- every other data file of Core
namespace gm {
namespace {

struct HbField {
    std::string key, label, type;                  // text | long | number | bool | select | list | pairs | rows
    std::vector<std::string> options;              // select
    std::vector<HbField> cols;                       // rows
    std::string optionsFrom;                       // select: the choices are the names of that kind of the Reference ("kin"), which the page asks for
    std::string optionsFile;                       // select: the choices are the entries of that file of Core and of the pack ("duration"), a kind of its own
    bool optionsUpper = false;                     // the names of optionsFrom are written in capitals (a spell is named so in another spell's prerequisite)
    std::string widget;                            // text: how the page asks for it (requirement: Word / Gesture / Material; range: Touch, meters...)
    std::string showKey, showValue;                // only shown while the field `showKey` has the value `showValue`
};

struct Section {
    std::string id, label, file, list;             // list: the key of the file that holds the entries
    bool entities = false;                         // a file of table entries: it begins with the header Core's file has (kind, categories...)
    std::vector<HbField> fields;
};

HbField text1(const char* key, const char* label) { return {key, label, "text", {}, {}}; }
HbField long1(const char* key, const char* label) { return {key, label, "long", {}, {}}; }
HbField number1(const char* key, const char* label) { return {key, label, "number", {}, {}}; }
HbField select1(const char* key, const char* label, std::vector<std::string> options) { return {key, label, "select", std::move(options), {}}; }
HbField kinChoice() {
    HbField f = {"kin", "Kin", "select", {}, {}};
    f.optionsFrom = "kin";
    f.showKey = "type";
    f.showValue = "kin";
    return f;
}
HbField widget1(const char* key, const char* label, const char* widget) {
    HbField f = {key, label, "text", {}, {}};
    f.widget = widget;
    return f;
}
HbField prerequisiteChoice() {
    HbField f = {"prerequisite", "Prerequisite", "select", {"", "Any School of Magic", "Animism", "Elementalism", "Mentalism"}, {}};
    f.optionsFrom = "spells";
    f.optionsUpper = true;
    return f;
}
HbField durationChoice() {
    HbField f = {"duration", "Duration", "select", {""}, {}};
    f.optionsFile = "duration";
    return f;
}
// a list of names taken from another kind of the Reference (the page offers them to pick, and a way to make a new one in that kind and come back)
HbField pickList(const char* key, const char* label, const char* from) {
    HbField f = {key, label, "list", {}, {}};
    f.optionsFrom = from;
    return f;
}
HbField list1(const char* key, const char* label) { return {key, label, "list", {}, {}}; }

std::string titleOf(const std::string& stem) {
    std::string out;
    bool start = true;
    for (char c : stem) {
        if (c == '-' || c == '_') {
            out += ' ';
            start = true;
        } else {
            out += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            start = false;
        }
    }
    return out;
}

const std::vector<Section>& dataSections() {
    static const std::vector<Section> all = [] {
        const std::vector<std::string> attributes = {"STR", "CON", "AGL", "INT", "WIL", "CHA"};
        const HbField supply = select1("supply", "Supply", {"", "Common", "Uncommon", "Rare"});
        std::vector<Section> s;
        s.push_back({"abilities", "Abilities", "abilities", "abilities", false,
                     {text1("name", "Name"), select1("type", "Type", {"", "kin", "heroic"}), kinChoice(), text1("wp_cost", "WP cost"), text1("requirement", "Requirement"), long1("description", "Description")}});
        s.push_back({"spells", "Spells", "spells", "spells", false,
                     {text1("name", "Name"), select1("school", "School", {"", "Animism", "Elementalism", "Mentalism", "General"}), {"trick", "Trick", "bool", {}, {}}, text1("rank", "Rank"), widget1("requirement", "Requirement", "requirement"),
                      prerequisiteChoice(), widget1("range", "Range", "range"), durationChoice(), select1("casting_time", "Casting time", {"", "Action", "Reaction", "Shift", "Stretch"}), long1("description", "Description")}});
        s.push_back({"skills", "Skills", "skills", "skills", false, {text1("name", "Name"), select1("attribute", "Attribute", attributes), select1("category", "Category (empty: a core skill)", {"", "weapon", "magic"}), long1("description", "Description")}});
        s.push_back({"kin", "Kin", "kin", "kin", false, {text1("name", "Name"), number1("movement", "Movement"), pickList("innate_abilities", "Innate abilities", "abilities"), long1("description", "Description")}});
        s.push_back({"professions", "Professions", "professions", "professions", false,
                     {text1("name", "Name"), select1("key_attribute", "Key attribute", attributes), pickList("skills", "Skills", "skills"), pickList("heroic_abilities", "Heroic abilities", "abilities"), long1("description", "Description")}});
        s.push_back({"weapons", "Weapons", "equipment", "weapons", false,
                     {text1("name", "Name"), select1("kind", "Kind", {"melee", "ranged"}), select1("grip", "Grip", {"", "1H", "2H"}), text1("str_req", "STR requirement"), text1("range", "Range"), text1("damage", "Damage"),
                      text1("durability", "Durability"), text1("cost", "Cost"), supply, text1("weight", "Weight"), text1("features", "Features"), list1("damage_types", "Damage types")}});
        s.push_back({"armor", "Armor", "equipment", "armor", false,
                     {text1("name", "Name"), select1("slot", "Slot", {"", "armor", "helmet"}), text1("armor_rating", "Armor rating"), list1("banes", "Banes"), text1("cost", "Cost"), supply, text1("weight", "Weight"),
                      {"armor_bonuses", "Armor bonuses", "rows", {}, {text1("damage_type", "Damage type"), number1("bonus", "Bonus")}}}});
        s.push_back({"gear", "Gear", "equipment", "gear", false,
                     {text1("name", "Name"), text1("category", "Category"), text1("cost", "Cost"), supply, text1("weight", "Weight"), long1("effect", "Effect")}});
        s.push_back({"traps", "Traps", "traps", "traps", false, {text1("name", "Name"), text1("damage", "Damage"), text1("roll", "Roll"), long1("description", "Description")}});
        s.push_back({"actions", "Actions", "actions", "actions", false, {text1("name", "Name"), text1("group", "Group (its id: actions, free-actions, movement, melee-combat...)"), long1("description", "Description")}});
        return s;
    }();
    return all;
}

// The files that are lists of table entries (a name, a description, columns): each one has its own section.
const std::vector<std::string>& entityFiles() {
    static const std::vector<std::string> files = {"age", "appearance", "damage-types", "duration", "fear", "hunting", "improvised-weapons", "injuries", "magic", "melee-demon-rolls",
                                                   "memento", "mishaps", "ranged-demon-rolls", "terrain", "treasure", "weakness"};
    return files;
}

// What Core's own file says before its entities (kind, category, rollable...): the homebrew file starts the same way.
json coreHeader(const std::string& coreDir, const std::string& stem) {
    json header = json::object();
    if (const auto root = jsonLoad(coreDir + "/" + stem + ".yaml"); root && root->is_object())
        for (auto it = root->begin(); it != root->end(); ++it)
            if (it.key() != "entities") header[it.key()] = it.value();
    return header;
}

std::vector<Section> allSections(const std::string& coreDir) {
    std::vector<Section> all = dataSections();
    for (const std::string& stem : entityFiles()) {
        Section s{stem, titleOf(stem), stem, "entities", true, {text1("name", "Name")}};
        std::vector<std::string> categories;
        const json header = coreHeader(coreDir, stem);
        if (const json* r = jsonFind(header, "rollable"); r && r->is_array())
            for (const json& c : *r)
                if (c.is_string()) categories.push_back(c.get<std::string>());
        if (categories.size() > 1) s.fields.push_back(select1("category", "Category", categories));
        s.fields.push_back(long1("description", "Description"));
        s.fields.push_back({"fields", "Columns (label and value)", "pairs", {}, {}});
        all.push_back(std::move(s));
    }
    return all;
}

const Section* findSection(const std::vector<Section>& all, const std::string& id) {
    for (const Section& s : all)
        if (s.id == id) return &s;
    return nullptr;
}

json fieldJson(const HbField& f) {
    json o = {{"key", f.key}, {"label", f.label}, {"type", f.type}};
    if (!f.options.empty()) o["options"] = f.options;
    if (!f.optionsFrom.empty()) o["optionsFrom"] = f.optionsFrom;
    if (f.optionsUpper) o["optionsUpper"] = true;
    if (!f.widget.empty()) o["widget"] = f.widget;
    if (!f.showKey.empty()) o["showWhen"] = {{"key", f.showKey}, {"value", f.showValue}};
    if (!f.cols.empty()) {
        o["cols"] = json::array();
        for (const HbField& c : f.cols) o["cols"].push_back(fieldJson(c));
    }
    return o;
}

// One value of the form, checked into `out[key]` (an empty value is simply not written).
bool checkValue(const HbField& f, const json& in, json& out, std::string& error) {
    const json* v = jsonFind(in, f.key.c_str());
    if (!v || v->is_null()) return true;
    const std::string what = f.label.empty() ? f.key : f.label;
    if (f.type == "text" || f.type == "long" || f.type == "select") {
        std::string s;
        if (v->is_number()) {
            s = v->dump();
        } else if (!v->is_string()) {
            error = what + " must be text";
            return false;
        } else {
            s = trimmed(v->get<std::string>());
        }
        if (s.size() > (f.type == "long" ? 4000u : 200u)) {
            error = what + " is too long";
            return false;
        }
        if (f.type == "select" && f.optionsFrom.empty() && f.optionsFile.empty() && !s.empty() && std::find(f.options.begin(), f.options.end(), s) == f.options.end()) {
            error = what + " is not one of the choices";
            return false;
        }
        if (!s.empty()) out[f.key] = s;
    } else if (f.type == "number") {
        if (v->is_string() && trimmed(v->get<std::string>()).empty()) return true;
        long long n = 0;
        const std::string asText = v->is_string() ? trimmed(v->get<std::string>()) : std::string();
        if (v->is_number_integer()) {
            n = v->get<long long>();
        } else if (!asText.empty() && asText.find_first_not_of("-0123456789") == std::string::npos) {
            n = std::atoll(asText.c_str());
        } else {
            error = what + " must be a whole number";
            return false;
        }
        out[f.key] = n;
    } else if (f.type == "bool") {
        if (!v->is_boolean()) {
            error = what + " must be yes or no";
            return false;
        }
        if (v->get<bool>()) out[f.key] = true;
    } else if (f.type == "list") {
        if (!v->is_array() || v->size() > 60) {
            error = what + " must be a list of at most 60";
            return false;
        }
        json list = json::array();
        for (const json& x : *v) {
            if (!x.is_string()) {
                error = what + " must be a list of texts";
                return false;
            }
            const std::string s = trimmed(x.get<std::string>());
            if (s.size() > 200) {
                error = what + " has an entry that is too long";
                return false;
            }
            if (!s.empty()) list.push_back(s);
        }
        if (!list.empty()) out[f.key] = list;
    } else if (f.type == "pairs") {
        if (!v->is_object() || v->size() > 30) {
            error = what + " must have at most 30 columns";
            return false;
        }
        json map = json::object();
        for (auto it = v->begin(); it != v->end(); ++it) {
            const std::string label = trimmed(it.key());
            if (label.empty()) continue;
            if (label.size() > 40 || !(it.value().is_string() || it.value().is_number())) {
                error = what + ": a column needs a short label and a value";
                return false;
            }
            const std::string value = trimmed(it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
            if (value.size() > 1000) {
                error = what + ": a value is too long";
                return false;
            }
            map[label] = value;
        }
        if (!map.empty()) out[f.key] = map;
    } else if (f.type == "rows") {
        if (!v->is_array() || v->size() > 30) {
            error = what + " must be a list of at most 30";
            return false;
        }
        json list = json::array();
        for (const json& r : *v) {
            if (!r.is_object()) {
                error = what + " has an entry that is not an object";
                return false;
            }
            json row = json::object();
            for (const HbField& c : f.cols)
                if (!checkValue(c, r, row, error)) return false;
            if (!row.empty()) list.push_back(row);
        }
        if (!list.empty()) out[f.key] = list;
    }
    return true;
}

// The pack's file as a whole (so what the form does not touch stays), and the list the section edits.
json readFile(const std::string& packDir, const Section& s, const std::string& coreDir) {
    if (const auto root = jsonLoad(packDir + "/" + s.file + ".yaml"); root && root->is_object()) return *root;
    return s.entities ? coreHeader(coreDir, s.file) : json::object();
}

bool writeFile(const std::string& packDir, const Section& s, json root, const json& list) {
    root[s.list] = list;
    SDL_CreateDirectory(packDir.c_str());
    if (!fs::isFile(packDir + "/manifest.yaml")) {
        const json manifest = {{"format", 1}, {"id", baseName(packDir)}, {"name", homebrewPackName(prefsOf(packDir))}, {"version", "1"}, {"description", "Made on the GM page."}};
        if (!fs::writeFile(packDir + "/manifest.yaml", manifest.dump(2))) return false;
    }
    return fs::writeFile(packDir + "/" + s.file + ".yaml", root.dump(2));
}

json entriesOf(const json& root, const Section& s) {
    const json* l = jsonFind(root, s.list.c_str());
    return l && l->is_array() ? *l : json::array();
}

}  // namespace

json homebrewSchema(const std::string& prefsDir, const std::string& coreDir) {
    json sections = json::array();
    for (const Section& s : allSections(coreDir)) {
        json fields = json::array();
        for (const HbField& f : s.fields) {
            json o = fieldJson(f);
            if (!f.optionsFile.empty()) {                                                // the choices Core has, and the ones the GM added
                std::vector<std::string> names = {""};
                for (const std::string& dir : {coreDir, customPackDir(prefsDir)})
                    if (const auto root = jsonLoad(dir + "/" + f.optionsFile + ".yaml"); root && root->is_object())
                        if (const json* l = jsonFind(*root, "entities"); l && l->is_array())
                            for (const json& e : *l)
                                if (e.is_object() && !jsonStr(e, "name").empty() && std::find(names.begin(), names.end(), jsonStr(e, "name")) == names.end()) names.push_back(jsonStr(e, "name"));
                o["options"] = names;
            }
            fields.push_back(std::move(o));
        }
        sections.push_back({{"id", s.id}, {"label", s.label}, {"fields", fields}});
    }
    return {{"pack", {{"id", baseName(customPackDir(prefsDir))}, {"name", homebrewPackName(prefsDir)}}}, {"sections", sections}};
}

json homebrewEntries(const std::string& packDir, const std::string& coreDir, const std::string& section) {
    const std::vector<Section> all = allSections(coreDir);
    const Section* s = findSection(all, section);
    if (!s) return nullptr;
    json out = json::array();
    int index = 0;
    for (json e : entriesOf(readFile(packDir, *s, coreDir), *s)) {
        if (e.is_object()) e["index"] = index;
        out.push_back(std::move(e));
        ++index;
    }
    return {{"entries", out}};
}

HomebrewResult saveHomebrewEntry(const std::string& packDir, const std::string& coreDir, const std::string& section, int index, const std::string& expect, const json& body, const std::string& replaces,
                                 const json& base) {
    HomebrewResult r;
    const std::vector<Section> all = allSections(coreDir);
    const Section* s = findSection(all, section);
    if (!s) {
        r.status = 404;
        r.error = "There is nothing to make of that kind.";
        return r;
    }
    if (!body.is_object()) {
        r.status = 400;
        r.error = "Send the entry as an object.";
        return r;
    }
    json entry = json::object();
    for (const HbField& f : s->fields)
        if (!checkValue(f, body, entry, r.error)) {
            r.status = 400;
            return r;
        }
    if (!entry.contains("name")) {
        r.status = 400;
        r.error = "It needs a name.";
        return r;
    }
    if (entry["name"].get<std::string>().size() > 80) {
        r.status = 400;
        r.error = "The name is too long (80 letters at most).";
        return r;
    }
    if (!replaces.empty()) {                                                          // a house rule: it takes the place of that card of another pack
        entry["replaces"] = replaces;
        if (base.is_object())
            for (auto it = base.begin(); it != base.end(); ++it) {                       // what the form has no field for (the name lists of a kin...) is carried over
                bool known = it.key() == "id" || it.key() == "replaces";
                for (const HbField& f : s->fields) known = known || f.key == it.key();
                if (!known) entry[it.key()] = it.value();
            }
    }
    json root = readFile(packDir, *s, coreDir), list = entriesOf(root, *s);
    if (index < 0) {
        list.push_back(entry);
        r.status = 201;
    } else {
        if (index >= static_cast<int>(list.size()) || !list[static_cast<size_t>(index)].is_object() || list[static_cast<size_t>(index)].value("name", "") != expect) {
            r.status = 404;
            r.error = "That entry is no longer there. Open the list again.";
            return r;
        }
        json& old = list[static_cast<size_t>(index)];
        for (auto it = old.begin(); it != old.end(); ++it) {                           // what the form has no field for stays as it was
            bool known = false;
            for (const HbField& f : s->fields) known = known || f.key == it.key();
            if (!known) entry[it.key()] = it.value();
        }
        old = entry;
    }
    if (!writeFile(packDir, *s, root, list)) {
        r.status = 500;
        r.error = "Could not save it.";
        return r;
    }
    r.body = entry;
    r.body["index"] = index < 0 ? static_cast<int>(list.size()) - 1 : index;
    return r;
}

HomebrewResult deleteHomebrewEntry(const std::string& packDir, const std::string& coreDir, const std::string& section, int index, const std::string& expect) {
    HomebrewResult r;
    const std::vector<Section> all = allSections(coreDir);
    const Section* s = findSection(all, section);
    json root = s ? readFile(packDir, *s, coreDir) : json::object();
    json list = s ? entriesOf(root, *s) : json::array();
    if (!s || index < 0 || index >= static_cast<int>(list.size()) || !list[static_cast<size_t>(index)].is_object() || list[static_cast<size_t>(index)].value("name", "") != expect) {
        r.status = 404;
        r.error = "That entry is no longer there. Open the list again.";
        return r;
    }
    list.erase(list.begin() + index);
    if (!writeFile(packDir, *s, root, list)) {
        r.status = 500;
        r.error = "Could not delete it.";
        return r;
    }
    r.body = {{"ok", true}};
    return r;
}

}  // namespace gm
