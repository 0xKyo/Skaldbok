#include "character.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <SDL3/SDL.h>

#include "encounter.h"
#include "fsutil.h"
#include "sheet_edit.h"
#include "jsonutil.h"

namespace gm {

const char* const kAttrShort[kAttrCount] = {"STR", "CON", "AGL", "INT", "WIL", "CHA"};
const char* const kAttrLong[kAttrCount] = {"Strength", "Constitution", "Agility", "Intelligence", "Willpower", "Charisma"};

int attrIndex(const std::string& s) {
    for (int i = 0; i < kAttrCount; ++i)
        if (s == kAttrShort[i]) return i;
    return -1;
}

std::string Character::displayName() const {
    std::string out = name.empty() ? "(unnamed)" : name;
    if (!nickname.empty()) out += " \"" + nickname + "\"";
    return out;
}

std::string Character::summary() const {
    std::string s = displayName() + "\n";
    s += kin.name + " " + profession.name + ", " + std::string(ageRule(age).label) + (player.empty() ? "" : "  (player: " + player + ")") + "\n\n";
    for (int i = 0; i < kAttrCount; ++i) s += std::string(kAttrShort[i]) + " " + std::to_string(attr[i]) + (i + 1 < kAttrCount ? "   " : "\n");
    s += "HP " + std::to_string(hp) + "/" + std::to_string(maxHp(*this)) + "   WP " + std::to_string(wp) + "/" + std::to_string(maxWp(*this));
    const std::string dbStr = damageBonus(attr[0]), dbAgl = damageBonus(attr[2]);
    s += "   Damage bonus STR " + (dbStr.empty() ? std::string("-") : dbStr) + " / AGL " + (dbAgl.empty() ? std::string("-") : dbAgl) + "\n";
    auto join = [](const std::vector<std::string>& v) {
        std::string out;
        for (const std::string& x : v) out += (out.empty() ? "" : ", ") + x;
        return out;
    };
    std::vector<std::string> trained, other;
    for (const SkillEntry& e : skills) {
        const std::string text = e.ref.name + " " + std::to_string(skillLevel(*this, &e, e.attribute));
        (e.trained ? trained : other).push_back(text);
    }
    if (!trained.empty()) s += "\nTrained skills: " + join(trained) + "\n";
    if (!other.empty()) s += "Other skills: " + join(other) + "\n";
    std::vector<std::string> names;
    for (const Ref& r : abilities) names.push_back(r.name);
    if (!names.empty()) s += "\nAbilities: " + join(names) + "\n";
    names.clear();
    for (const Ref& r : spells) names.push_back(r.name);
    if (!names.empty()) s += "Spells: " + join(names) + "\n";
    names.clear();
    for (const Item& it : weapons) names.push_back(it.name);
    if (!armor.name.empty()) names.push_back(armor.name);
    if (!helmet.name.empty()) names.push_back(helmet.name);
    for (const Item& it : inventory) names.push_back(it.count > 1 ? it.name + " x" + std::to_string(it.count) : it.name);
    for (const std::string& t : tinyItems) names.push_back(t);
    if (!names.empty()) s += "\nGear: " + join(names) + "\n";
    if (gold || silver || copper) s += "Coins: " + std::to_string(gold) + " gold, " + std::to_string(silver) + " silver, " + std::to_string(copper) + " copper\n";
    if (!weakness.empty()) s += "\nWeakness: " + weakness + "\n";
    if (!memento.empty()) s += "Memento: " + memento + "\n";
    if (!appearance.empty()) s += "Appearance: " + appearance + "\n";
    return s;
}

// ------------------------------------------------------------------------------------------ rules

int baseChance(int a) {
    if (a <= 5) return 3;
    if (a <= 8) return 4;
    if (a <= 12) return 5;
    if (a <= 15) return 6;
    return 7;
}

int movementModifier(int agl) {
    if (agl <= 6) return -4;
    if (agl <= 9) return -2;
    if (agl <= 12) return 0;
    if (agl <= 15) return 2;
    return 4;
}

std::string damageBonus(int v) {
    if (v <= 12) return "";
    if (v <= 16) return "D4";
    return "D6";
}

int encumbranceLimit(const Character& c) {
    int limit = (c.attr[0] + 1) / 2;
    for (const Item& it : c.inventory) {
        std::string n;
        for (char ch : it.name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (n == "backpack") {
            limit += 2;                        // one backpack at a time
            break;
        }
    }
    return limit;
}

int carriedItems(const Character& c) {
    int n = 0;
    for (const Item& it : c.inventory) {
        std::string name;
        for (char ch : it.name) name += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        n += name.contains("food ration") ? (it.count + 3) / 4 : std::max(1, it.count);
    }
    return n;
}

int maxHp(const Character& c) { return std::max(1, c.attr[1] + c.hpBonus); }
int maxWp(const Character& c) { return std::max(1, c.attr[4] + c.wpBonus); }

const AgeRule& ageRule(const std::string& age) {
    static const AgeRule rules[] = {
        {"young", "Young", 8, {0, 1, 1, 0, 0, 0}, "AGL and CON +1; 6+2 trained skills"},
        {"adult", "Adult", 10, {0, 0, 0, 0, 0, 0}, "no change; 6+4 trained skills"},
        {"old", "Old", 12, {-2, -2, -2, 1, 1, 0}, "STR, AGL and CON -2, INT and WIL +1; 6+6 trained skills"},
    };
    for (const AgeRule& r : rules)
        if (age == r.id) return r;
    return rules[1];
}

void applyAge(const int rolled[kAttrCount], const std::string& age, int out[kAttrCount]) {
    const AgeRule& r = ageRule(age);
    for (int i = 0; i < kAttrCount; ++i) out[i] = std::clamp(rolled[i] + r.attrMod[i], 1, 18);
}

int skillLevel(const Character& c, const SkillEntry* entry, const std::string& attribute) {
    if (entry && entry->level > 0) return entry->level;
    const int a = attrIndex(attribute.empty() && entry ? entry->attribute : attribute);
    return a < 0 ? 0 : baseChance(c.attr[a]);
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
    for (int i = 0; i < kAttrCount; ++i) at[kAttrShort[i]] = attr[i];
    j["attributes"] = at;
    j["hp"] = hp;
    j["wp"] = wp;
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

bool Character::fromJson(const std::string& text, Character& out, std::string* error) {
    json j;
    if (!jsonParse(text, j, error)) return false;
    if (!j.is_object()) {
        if (error) *error = "a character file must be a JSON object";
        return false;
    }
    Character c;
    c.format = jsonInt(j, "format", 1);
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
    if (const json* at = jsonFind(j, "attributes"))
        for (int i = 0; i < kAttrCount; ++i) c.attr[i] = std::clamp(jsonInt(*at, kAttrShort[i], 10), 1, 30);
    c.hpBonus = jsonInt(j, "hp_bonus");
    c.wpBonus = jsonInt(j, "wp_bonus");
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
            sheet::merge(doc, sheet::diff(base, edited));
            doc["revision"] = jsonInt(doc, "revision") + 1;
            doc["updated_at"] = item.updatedAt;
            if (!fs::writeFile(path, doc.dump(2) + "\n")) return false;
            const std::string id = item.id;
            Character::fromJson(doc.dump(), item, nullptr);            // what is now in the file, other people's changes included
            item.id = id;
            return true;
        }
    }
    item.revision = (memory ? memory->revision : 0) + 1;
    return fs::writeFile(path, item.toJson());
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
    if (fs::writeFile(path, c.toJson())) return true;
    if (error) *error = SDL_GetError();
    return false;
}

}  // namespace gm
