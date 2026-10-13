#include "game/character.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>

#include <SDL3/SDL.h>

#include "game/encounter.h"
#include "parsing/fsutil.h"
#include "game/sheet_edit.h"
#include "parsing/fts.h"
#include "parsing/i18n.h"
#include "parsing/jsonutil.h"

namespace gm {

std::string legacyAttributeId(const std::string& code) {
    static const std::pair<const char*, const char*> codes[] = {{"STR", kStrength}, {"CON", kConstitution}, {"AGL", kAgility},
                                                                {"INT", kIntelligence}, {"WIL", kWillpower}, {"CHA", kCharisma}};
    for (const auto& [old, id] : codes)
        if (code == old) return id;
    return code;
}

int Scores::get(const std::string& id, int otherwise) const {
    for (const auto& [k, v] : items_)
        if (k == id) return v;
    return otherwise;
}

void Scores::set(const std::string& id, int score) {
    for (auto& [k, v] : items_)
        if (k == id) {
            v = score;
            return;
        }
    items_.emplace_back(id, score);
}

bool Scores::has(const std::string& id) const {
    return std::ranges::any_of(items_, [&](const auto& item) { return item.first == id; });
}

std::string Character::displayName() const {
    std::string out = name.empty() ? i18n::tr("(unnamed)") : name;
    if (!nickname.empty()) out += " \"" + nickname + "\"";
    return out;
}

// (its words in the current language, i18n.h; the names are the sheet's own)
std::string Character::summary(const AttributeSet& rules, const std::function<std::string(const std::string&)>& abbr, const std::string& ageName) const {
    using i18n::tr;
    std::string s = displayName() + "\n";
    s += kin.name + " " + profession.name + ", " + ageName + (player.empty() ? "" : "  (" + tr("player") + ": " + player + ")") + "\n\n";
    for (size_t i = 0; i < rules.attributes().size(); ++i) {
        const std::string& id = rules.attributes()[i].id;
        s += abbr(id) + " " + std::to_string(attr.get(id)) + (i + 1 < rules.attributes().size() ? "   " : "\n");
    }
    s += tr("HP") + " " + std::to_string(hp) + "/" + std::to_string(maxHp(*this)) + "   " + tr("WP") + " " + std::to_string(wp) + "/" + std::to_string(maxWp(*this));
    s += "   " + tr("Damage bonus");
    const Lookup* bonus = rules.lookup("damage-bonus");
    for (size_t i = 0; bonus && i < bonus->attributes.size(); ++i) {
        const std::string value = damageBonus(rules, attr.get(bonus->attributes[i]));
        s += (i ? " / " : " ") + abbr(bonus->attributes[i]) + " " + (value.empty() ? std::string("-") : value);
    }
    s += "\n";
    auto join = [](const std::vector<std::string>& v) {
        std::string out;
        for (const std::string& x : v) out += (out.empty() ? "" : ", ") + x;
        return out;
    };
    std::vector<std::string> trained, other;
    for (const SkillEntry& e : skills) {
        const std::string text = e.ref.name + " " + std::to_string(skillLevel(rules, *this, &e, e.attribute));
        (e.trained ? trained : other).push_back(text);
    }
    if (!trained.empty()) s += "\n" + tr("Trained skills") + ": " + join(trained) + "\n";
    if (!other.empty()) s += tr("Other skills") + ": " + join(other) + "\n";
    std::vector<std::string> names;
    for (const Ref& r : abilities) names.push_back(r.name);
    if (!names.empty()) s += "\n" + tr("Abilities") + ": " + join(names) + "\n";
    names.clear();
    for (const Ref& r : spells) names.push_back(r.name);
    if (!names.empty()) s += tr("Spells") + ": " + join(names) + "\n";
    names.clear();
    for (const Item& it : weapons) names.push_back(it.name);
    if (!armor.name.empty()) names.push_back(armor.name);
    if (!helmet.name.empty()) names.push_back(helmet.name);
    for (const Item& it : inventory) names.push_back(it.count > 1 ? it.name + " x" + std::to_string(it.count) : it.name);
    for (const std::string& t : tinyItems) names.push_back(t);
    if (!names.empty()) s += "\n" + tr("Gear") + ": " + join(names) + "\n";
    if (gold || silver || copper)
        s += tr("Coins") + ": " + std::to_string(gold) + " " + tr("gold") + ", " + std::to_string(silver) + " " + tr("silver") + ", " + std::to_string(copper) + " " + tr("copper") + "\n";
    if (!weakness.empty()) s += "\n" + tr("Weakness") + ": " + weakness + "\n";
    if (!memento.empty()) s += tr("Memento") + ": " + memento + "\n";
    if (!appearance.empty()) s += tr("Appearance") + ": " + appearance + "\n";
    return s;
}

// ------------------------------------------------------------------------------------------ rules

int baseChance(const AttributeSet& rules, int score) { return rules.number("base-chance", score); }

int movementModifier(const AttributeSet& rules, int agility) { return rules.number("movement-modifier", agility); }

std::string damageBonus(const AttributeSet& rules, int score) { return rules.value("damage-bonus", score); }

// A backpack weighs nothing, so it is a tiny item and lives in the tiny items, not in the inventory (see moveTinyItems): it counts in either.
static bool holdsBackpack(const Character& c) {
    const auto lower = [](const std::string& s) {
        std::string n;
        for (char ch : s) n += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return n;
    };
    for (const Item& it : c.inventory)
        if (lower(it.name) == "backpack" || it.key.ends_with("/gear/backpack")) return true;
    for (const std::string& tiny : c.tinyItems) {                                  // "Backpack", or "Backpack x2" (a second one adds nothing)
        const std::string n = lower(tiny);
        if (n == "backpack" || n.starts_with("backpack x")) return true;
    }
    return false;
}

int encumbranceLimit(const Character& c) {
    return (c.attr.get(kStrength) + 1) / 2 + (holdsBackpack(c) ? 2 : 0);          // one backpack at a time
}

double itemWeight(const Item& it) {
    if (it.weight >= 0) return it.weight;
    std::string name;
    for (char ch : it.name) name += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return name.contains("food ration") ? 0.25 : 1.0;
}

double weightFromText(const std::string& text) {
    const std::string t = trimmed(text);
    if (t == "\xE2\x80\x94" || t == "-" || t == "0") return 0;                      // "—": weighs nothing
    const auto slash = t.find('/');
    char* end = nullptr;
    if (slash != std::string::npos) {
        const double num = std::strtod(t.c_str(), &end), den = std::strtod(t.c_str() + slash + 1, nullptr);
        return den > 0 && num >= 0 ? num / den : -1;
    }
    const double v = std::strtod(t.c_str(), &end);
    return end != t.c_str() && *end == '\0' && v >= 0 ? v : -1;
}

int carriedItems(const Character& c) {
    int n = 0;
    for (const Item& it : c.inventory) n += static_cast<int>(std::ceil(itemWeight(it) * std::max(1, it.count) - 1e-9));
    return n;
}

int maxHp(const Character& c) { return std::max(1, c.attr.get(kConstitution) + c.hpBonus); }
int maxWp(const Character& c) { return std::max(1, c.attr.get(kWillpower) + c.wpBonus); }

Scores applyAge(const AttributeSet& rules, const std::vector<int>& rolled, const AgeDef& r) {
    Scores out;
    for (size_t i = 0; i < rules.attributes().size(); ++i) {
        const std::string& id = rules.attributes()[i].id;
        out.set(id, std::clamp((i < rolled.size() ? rolled[i] : kDefaultScore) + r.modFor(id), 1, 18));
    }
    return out;
}

int skillLevel(const AttributeSet& rules, const Character& c, const SkillEntry* entry, const std::string& attribute) {
    if (entry && entry->level > 0) return entry->level;
    const std::string id = rules.idOf(attribute.empty() && entry ? entry->attribute : attribute);
    return id.empty() ? 0 : baseChance(rules, c.attr.get(id));
}

// ------------------------------------------------------------------------------------------ JSON

namespace {

json refJson(const Ref& r) { return json{{"key", r.key}, {"name", r.name}}; }

Ref refFrom(const json* v) {
    Ref r;
    if (!v) return r;
    if (v->is_string()) r.name = v->get<std::string>();
    else if (v->is_object()) {
        r.key = jsonStr(*v, "key");
        r.name = jsonStr(*v, "name");
    }
    return r;
}

json itemJson(const Item& it) {
    json j = {{"name", it.name}};
    if (!it.key.empty()) j["key"] = it.key;
    if (it.count != 1) j["count"] = it.count;
    if (!it.note.empty()) j["note"] = it.note;
    if (it.weight >= 0) j["weight"] = it.weight;
    return j;
}

Item itemFrom(const json& j) {
    Item it;
    if (j.is_string()) {
        it.name = j.get<std::string>();
        return it;
    }
    it.name = jsonStr(j, "name");
    it.key = jsonStr(j, "key");
    it.count = std::max(1, jsonInt(j, "count", 1));
    it.note = jsonStr(j, "note");
    if (const json* w = jsonFind(j, "weight"); w && w->is_number() && w->get<double>() >= 0) it.weight = w->get<double>();
    return it;
}

}  // namespace

std::string Character::toJson() const {
    json j;
    j["format"] = format;
    j["revision"] = revision;
    j["id"] = id;
    j["name"] = name;
    j["nickname"] = nickname;
    j["player"] = player;
    j["age"] = age;
    j["kin"] = refJson(kin);
    j["profession"] = refJson(profession);
    j["school"] = school;
    json at = json::object();
    for (const auto& [id, score] : attr.items()) at[id] = score;
    j["attributes"] = at;
    j["hp"] = hp;
    j["wp"] = wp;
    j["round_rest"] = roundRest;
    j["stretch_rest"] = stretchRest;
    j["death_successes"] = deathSuccesses;
    j["death_failures"] = deathFailures;
    j["hp_bonus"] = hpBonus;
    j["wp_bonus"] = wpBonus;
    json cond = json::array();
    for (int i = 0; i < 6; ++i)
        if (conditions & (1u << i)) cond.push_back(kConditions[i].name);
    j["conditions"] = cond;
    json sk = json::array();
    for (const SkillEntry& s : skills)
        sk.push_back({{"key", s.ref.key}, {"name", s.ref.name}, {"attribute", s.attribute}, {"level", s.level},
                      {"trained", s.trained}, {"marked", s.marked}});
    j["skills"] = sk;
    json ab = json::array(), sp = json::array();
    for (const Ref& r : abilities) ab.push_back(refJson(r));
    for (const Ref& r : spells) sp.push_back(refJson(r));
    j["abilities"] = ab;
    j["spells"] = sp;
    json w = json::array();
    for (const Item& it : weapons) w.push_back(itemJson(it));
    j["weapons"] = w;
    j["armor"] = armor.name.empty() ? json(nullptr) : itemJson(armor);
    j["helmet"] = helmet.name.empty() ? json(nullptr) : itemJson(helmet);
    json inv = json::array();
    for (const Item& it : inventory) inv.push_back(itemJson(it));
    j["inventory"] = inv;
    j["tiny_items"] = tinyItems;
    j["coins"] = {{"gold", gold}, {"silver", silver}, {"copper", copper}};
    j["weakness"] = weakness;
    j["memento"] = memento;
    j["appearance"] = appearance;
    j["notes"] = notes;
    if (locked) j["locked"] = true;
    if (!reviews.empty()) {
        json r = json::object();
        for (const auto& [key, v] : reviews) r[key] = {{"status", v.status}, {"value", v.value}, {"at", v.at}};
        j["reviews"] = r;
    }
    j["created_at"] = createdAt;
    j["updated_at"] = updatedAt;
    return j.dump(2) + "\n";
}

void Character::migrateJson(json& doc) {
    if (!doc.is_object()) return;
    // a value the codes of the old files named: both spellings may be there, and what is written with the id wins
    if (auto at = doc.find("attributes"); at != doc.end() && at->is_object()) {
        json fixed = json::object();
        for (auto it = at->begin(); it != at->end(); ++it)
            if (legacyAttributeId(it.key()) == it.key()) fixed[it.key()] = it.value();
        for (auto it = at->begin(); it != at->end(); ++it)
            if (const std::string id = legacyAttributeId(it.key()); id != it.key() && !fixed.contains(id)) fixed[id] = it.value();
        *at = fixed;
    }
    if (auto sk = doc.find("skills"); sk != doc.end() && sk->is_array())
        for (json& s : *sk)
            if (s.is_object() && s.contains("attribute") && s["attribute"].is_string()) s["attribute"] = legacyAttributeId(s["attribute"].get<std::string>());
    if (auto rv = doc.find("reviews"); rv != doc.end() && rv->is_object()) {
        json fixed = json::object();
        for (auto it = rv->begin(); it != rv->end(); ++it) {
            std::string key = it.key();
            if (key.starts_with("attr:")) key = "attr:" + legacyAttributeId(key.substr(5));
            fixed[key] = it.value();
        }
        *rv = fixed;
    }
    if (doc.contains("format") && doc["format"].is_number() && doc["format"].get<int>() < kCharacterFormat) doc["format"] = kCharacterFormat;
}

bool Character::fromJson(const std::string& text, Character& out, std::string* error) {
    json j;
    if (!jsonParse(text, j, error)) return false;
    if (!j.is_object()) {
        if (error) *error = "a character file must be a JSON object";
        return false;
    }
    migrateJson(j);
    Character c;
    c.format = std::max(kCharacterFormat, jsonInt(j, "format", kCharacterFormat));
    c.revision = jsonInt(j, "revision");
    c.id = jsonStr(j, "id");
    c.name = jsonStr(j, "name");
    c.nickname = jsonStr(j, "nickname");
    c.player = jsonStr(j, "player");
    c.age = jsonStr(j, "age", "adult");
    if (c.age != "young" && c.age != "old") c.age = "adult";
    c.kin = refFrom(jsonFind(j, "kin"));
    c.profession = refFrom(jsonFind(j, "profession"));
    c.school = jsonStr(j, "school");
    if (const json* at = jsonFind(j, "attributes"); at && at->is_object())
        for (auto it = at->begin(); it != at->end(); ++it) c.attr.set(it.key(), std::clamp(jsonInt(*at, it.key().c_str(), kDefaultScore), 1, 30));
    for (const char* id : {kStrength, kConstitution, kAgility, kIntelligence, kWillpower, kCharisma})   // a file that names none of them: all average
        if (!c.attr.has(id) && !jsonFind(j, "attributes")) c.attr.set(id, kDefaultScore);
    c.hpBonus = jsonInt(j, "hp_bonus");
    c.wpBonus = jsonInt(j, "wp_bonus");
    c.roundRest = jsonBool(j, "round_rest");
    c.stretchRest = jsonBool(j, "stretch_rest");
    c.deathSuccesses = std::clamp(jsonInt(j, "death_successes"), 0, 3);
    c.deathFailures = std::clamp(jsonInt(j, "death_failures"), 0, 3);
    c.hp = std::clamp(jsonInt(j, "hp", maxHp(c)), 0, 99);
    c.wp = std::clamp(jsonInt(j, "wp", maxWp(c)), 0, 99);
    for (const std::string& n : jsonStrings(j, "conditions"))
        for (int i = 0; i < 6; ++i)
            if (n == kConditions[i].name) c.conditions |= 1u << i;
    if (const json* sk = jsonFind(j, "skills"); sk && sk->is_array())
        for (const json& s : *sk) {
            SkillEntry e;
            e.ref.key = jsonStr(s, "key");
            e.ref.name = jsonStr(s, "name");
            e.attribute = jsonStr(s, "attribute");
            e.level = std::clamp(jsonInt(s, "level"), 0, 30);
            e.trained = jsonBool(s, "trained");
            e.marked = jsonBool(s, "marked");
            if (!e.ref.name.empty()) c.skills.push_back(std::move(e));
        }
    for (const char* k : {"abilities", "spells"})
        if (const json* a = jsonFind(j, k); a && a->is_array())
            for (const json& r : *a) {
                Ref ref = refFrom(&r);
                if (!ref.empty()) (std::string(k) == "abilities" ? c.abilities : c.spells).push_back(std::move(ref));
            }
    if (const json* w = jsonFind(j, "weapons"); w && w->is_array())
        for (const json& it : *w) c.weapons.push_back(itemFrom(it));
    if (const json* a = jsonFind(j, "armor")) c.armor = itemFrom(*a);
    if (const json* h = jsonFind(j, "helmet")) c.helmet = itemFrom(*h);
    if (const json* inv = jsonFind(j, "inventory"); inv && inv->is_array())
        for (const json& it : *inv) c.inventory.push_back(itemFrom(it));
    c.tinyItems = jsonStrings(j, "tiny_items");
    if (const json* co = jsonFind(j, "coins")) {
        c.gold = std::max(0, jsonInt(*co, "gold"));
        c.silver = std::max(0, jsonInt(*co, "silver"));
        c.copper = std::max(0, jsonInt(*co, "copper"));
    }
    c.weakness = jsonStr(j, "weakness");
    c.memento = jsonStr(j, "memento");
    c.appearance = jsonStr(j, "appearance");
    c.notes = jsonStr(j, "notes");
    c.locked = jsonBool(j, "locked");
    if (const json* r = jsonFind(j, "reviews"); r && r->is_object())
        for (auto it = r->begin(); it != r->end(); ++it)
            if (it.value().is_object()) c.reviews[it.key()] = {jsonStr(it.value(), "status"), jsonStr(it.value(), "value"), jsonStr(it.value(), "at")};
    c.createdAt = jsonStr(j, "created_at");
    c.updatedAt = jsonStr(j, "updated_at");
    out = std::move(c);
    return true;
}

// ------------------------------------------------------------------------------------------ files

bool CharacterStore::commit(Character& item, const Character* memory) {
    const std::string path = pathOf(item.id);
    if (memory) {
        json base, edited;
        jsonParse(memory->toJson(), base, nullptr);
        jsonParse(item.toJson(), edited, nullptr);
        if (const auto disk = jsonLoad(path)) {
            json doc = *disk;
            Character::migrateJson(doc);                                // a file written before attributes had ids is brought up to date first
            sheet::merge(doc, sheet::diff(base, edited));
            doc["revision"] = jsonInt(doc, "revision") + 1;
            doc["updated_at"] = item.updatedAt;
            if (!fs::writeFile(path, jsonToYaml(doc))) return false;
            const std::string id = item.id;
            Character::fromJson(doc.dump(), item, nullptr);            // what is now in the file, other people's changes included
            item.id = id;
            return true;
        }
    }
    item.revision = (memory ? memory->revision : 0) + 1;
    json j;
    if (!jsonParse(item.toJson(), j, nullptr)) return false;
    return fs::writeFile(path, jsonToYaml(j));
}

bool CharacterStore::importFile(const std::string& path, std::string* newId, std::string* error) {
    const auto text = fs::readFile(path);
    if (!text) {
        if (error) *error = SDL_GetError();
        return false;
    }
    Character c;
    if (!Character::fromJson(*text, c, error)) return false;
    c.id.clear();                          // always a new identity: importing never overwrites a character
    c.createdAt.clear();
    if (!save(c, error)) return false;
    if (newId) *newId = c.id;
    return true;
}

bool CharacterStore::exportFile(const Character& c, const std::string& path, std::string* error) const {
    json j;
    if (!jsonParse(c.toJson(), j, nullptr)) {
        if (error) *error = "serialisation error";
        return false;
    }
    if (fs::writeFile(path, jsonToYaml(j))) return true;
    if (error) *error = SDL_GetError();
    return false;
}

}  // namespace gm
