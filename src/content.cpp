#include "content.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iterator>

#include <SDL3/SDL.h>
#include <sqlite3.h>

#include "fts.h"
#include "jsonutil.h"
#include "sql.h"

namespace gm {
namespace {

constexpr int kFormat = 1;                    // the pack format this build understands
constexpr size_t kMaxFileBytes = 64u << 20;   // a pack file bigger than this is not a pack file
constexpr size_t kMaxWarnings = 60;

std::string slugOf(const std::string& s) {
    std::string out;
    bool dash = false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            if (dash && !out.empty()) out += '-';
            out += static_cast<char>(std::tolower(c));
            dash = false;
        } else {
            dash = true;
        }
    }
    return out;
}

bool validPackId(const std::string& id) {
    if (id.empty() || id.size() > 64 || !(std::islower(static_cast<unsigned char>(id[0])) || std::isdigit(static_cast<unsigned char>(id[0]))))
        return false;
    for (unsigned char c : id)
        if (!(std::islower(c) || std::isdigit(c) || c == '-' || c == '_')) return false;
    return true;
}

// A pack may only point at files inside its own folder.
bool safeRelPath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p[0] == '\\' || p.contains(':')) return false;
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t end = p.find_first_of("/\\", pos);
        if (end == std::string::npos) end = p.size();
        if (p.substr(pos, end - pos) == "..") return false;
        pos = end + 1;
    }
    return true;
}

bool isFile(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool readTextFile(const std::string& path, std::string& out, std::string* error) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
        if (error) *error = "file not found";
        return false;
    }
    if (info.size > kMaxFileBytes) {
        if (error) *error = "file is too large";
        return false;
    }
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        if (error) *error = SDL_GetError();
        return false;
    }
    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
    return true;
}

// "1", "1-2", "1–2", "3 to 4": the rolls a table row covers.
bool parseRoll(const std::string& text, int& lo, int& hi) {
    int nums[2] = {0, 0};
    int n = 0;
    for (size_t i = 0; i < text.size() && n < 2;) {
        if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            size_t j = i;
            int v = 0;
            while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) v = v * 10 + (text[j++] - '0');
            nums[n++] = v;
            i = j;
        } else {
            ++i;
        }
    }
    if (n == 0) return false;
    lo = nums[0];
    hi = n == 2 ? nums[1] : nums[0];
    if (hi < lo) std::swap(lo, hi);
    return true;
}

std::string normalizeDice(std::string d) {
    d = trimmed(d);
    for (char& c : d) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (d.size() > 2 && d[0] == '1' && d[1] == 'D') d.erase(0, 1);
    return d;
}

unsigned colorFor(const std::string& text) {
    unsigned h = 2166136261u;
    for (unsigned char c : text) h = (h ^ c) * 16777619u;
    const float hue = static_cast<float>(h % 360u) / 60.0f, s = 0.55f, v = 0.95f;
    const int i = static_cast<int>(hue);
    const float f = hue - static_cast<float>(i), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r = v, g = t, b = p;
    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
    }
    return (static_cast<unsigned>(r * 255) << 16) | (static_cast<unsigned>(g * 255) << 8) | static_cast<unsigned>(b * 255);
}

void addField(std::vector<Field>& out, const std::string& label, const std::string& value) {
    if (!value.empty()) out.push_back({label, value});
}

std::string joinList(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : sep) + s;
    return out;
}

const char* const kFiles[] = {"spells", "abilities", "skills", "kin", "professions", "weapons", "armor", "gear", "tables", "creatures"};

}  // namespace

// ------------------------------------------------------------------------------------- manifest

bool readManifest(const std::string& dir, PackInfo& out) {
    out.dir = dir;
    std::string text, err;
    if (!readTextFile(dir + "/manifest.json", text, &err)) {
        out.error = "manifest.json: " + err;
        return false;
    }
    json m;
    if (!jsonParse(text, m, &err)) {
        out.error = "manifest.json: " + err;
        return false;
    }
    if (!m.is_object()) {
        out.error = "manifest.json must be a JSON object";
        return false;
    }
    if (jsonInt(m, "format", kFormat) > kFormat) {
        out.error = "made for a newer version of the app (pack format " + std::to_string(jsonInt(m, "format")) + ")";
        return false;
    }
    out.id = jsonStr(m, "id");
    if (!validPackId(out.id)) {
        out.error = "manifest.json: \"id\" must be lowercase letters, digits, - or _ (like \"my-tome\")";
        return false;
    }
    out.name = jsonStr(m, "name", out.id);
    out.version = jsonStr(m, "version");
    out.author = jsonStr(m, "author");
    out.description = jsonStr(m, "description");
    return true;
}

// ------------------------------------------------------------------------------------- loading

struct ContentStore::Loader {
    ContentStore& st;
    PackInfo& pk;
    const json manifest;
    std::map<std::string, int> srcByKey;
    int firstSource = 0;
    std::set<std::string> usedIds[kKindCount];

    Loader(ContentStore& store, PackInfo& pack, json man) : st(store), pk(pack), manifest(std::move(man)) {}

    void warn(const std::string& msg) {
        if (pk.warnings.size() < kMaxWarnings) pk.warnings.push_back(msg);
        else if (pk.warnings.size() == kMaxWarnings) pk.warnings.push_back("(more problems not listed)");
    }

    void addSource(const std::string& key, const std::string& title, const std::string& label, bool homebrew) {
        SourceInfo s;
        s.id = st.sources_.empty() ? 1 : st.sources_.back().id + 1;
        if (homebrew) s.id = std::max(s.id, 100);
        s.key = key;
        s.packId = pk.id;
        s.title = title;
        s.label = label;
        s.homebrew = homebrew;
        s.color = colorFor(pk.id + key);
        st.sources_.push_back(s);
        srcByKey[key] = s.id;
        pk.sourceIds.push_back(s.id);
        if (!firstSource) firstSource = s.id;
    }

    void setupSources() {
        const json* list = jsonFind(manifest, "sources");
        if (pk.core) {
            // the Core books already exist (from the database); the manifest may rename their badges
            for (const SourceInfo& s : st.sources_)
                if (s.book) {
                    srcByKey[s.key] = s.id;
                    pk.sourceIds.push_back(s.id);
                    if (!firstSource) firstSource = s.id;
                }
            if (list && list->is_array())
                for (const json& o : *list) {
                    const std::string key = jsonStr(o, "key"), shortName = jsonStr(o, "short");
                    for (SourceInfo& s : st.sources_)
                        if (s.book && s.key == key && !shortName.empty()) s.label = shortName;
                }
            return;
        }
        if (list && list->is_array())
            for (const json& o : *list) {
                const std::string key = jsonStr(o, "key");
                if (key.empty() || srcByKey.count(key)) continue;
                const std::string title = jsonStr(o, "title", jsonStr(o, "short", key));
                addSource(key, title, "Homebrew · " + jsonStr(o, "short", title), true);
            }
        if (srcByKey.empty()) addSource(pk.id, pk.name, "Homebrew · " + pk.name, true);
    }

    int sourceFor(const json& o, const std::string& what) {
        const std::string key = jsonStr(o, "source");
        if (key.empty()) return firstSource;
        auto it = srcByKey.find(key);
        if (it != srcByKey.end()) return it->second;
        warn(what + ": unknown source \"" + key + "\" (using the pack's first source)");
        return firstSource;
    }

    // "<pack>/<kind>/<id>"; ids are unique inside a pack and kind.
    std::string makeKey(Kind k, const json& o, const std::string& name) {
        std::string id = slugOf(jsonStr(o, "id"));
        if (id.empty()) id = slugOf(name);
        if (id.empty()) id = "entry";
        std::string cand = id;
        for (int n = 2; !usedIds[static_cast<int>(k)].insert(cand).second; ++n) cand = id + "-" + std::to_string(n);
        if (cand != id && !jsonStr(o, "id").empty()) warn(std::string(kindKey(k)) + " \"" + name + "\": id \"" + id + "\" is used twice");
        return pk.id + "/" + kindKey(k) + "/" + cand;
    }

    void pageInfo(const json& o, int sourceId, PageRef& ref, std::string& note) {
        const int page = jsonInt(o, "page"), printed = jsonInt(o, "printed_page");
        const SourceInfo* si = st.source(sourceId);
        if (si && si->book && page > 0) {
            ref.sourceId = sourceId;
            ref.page = page;
            ref.printed = printed;
        } else if (printed > 0 || page > 0) {
            note = "p." + std::to_string(printed > 0 ? printed : page);
        }
    }

    // Extra display fields any card may carry: "fields": {"Label": "value"}
    void extraFields(const json& o, std::vector<Field>& out) {
        const json* f = jsonFind(o, "fields");
        if (!f) return;
        if (f->is_object()) {
            for (auto it = f->begin(); it != f->end(); ++it) addField(out, it.key(), jsonText(it.value()));
        } else if (f->is_array()) {
            for (const json& e : *f)
                if (e.is_object()) addField(out, jsonStr(e, "label"), jsonStr(e, "value"));
                else if (e.is_array() && e.size() >= 2) addField(out, jsonText(e[0]), jsonText(e[1]));
        }
    }

    Entry& push(Kind k, const json& o, const std::string& name) {
        Entry e;
        e.kind = k;
        e.key = makeKey(k, o, name);
        e.sourceId = sourceFor(o, std::string(kindKey(k)) + " \"" + name + "\"");
        e.title = name;
        pageInfo(o, e.sourceId, e.ref, e.pageNote);
        auto& v = st.entries_[static_cast<int>(k)];
        e.id = static_cast<int>(v.size()) + 1;
        st.byKey_[static_cast<int>(k)][e.key] = e.id;
        v.push_back(std::move(e));
        ++pk.counts[static_cast<int>(k)];
        return v.back();
    }

    // Reads dir/<file>.json and calls fn for every named object of its array.
    bool eachObject(const std::string& file, const std::function<void(const json&, const std::string&)>& fn) {
        const std::string path = pk.dir + "/" + file + ".json";
        if (!isFile(path)) return true;
        std::string text, err;
        if (!readTextFile(path, text, &err)) {
            pk.error = file + ".json: " + err;
            return false;
        }
        json root;
        if (!jsonParse(text, root, &err)) {
            pk.error = file + ".json: " + err;
            return false;
        }
        if (jsonInt(root, "format", kFormat) > kFormat) {
            pk.error = file + ".json: made for a newer version of the app";
            return false;
        }
        const json* arr = root.is_array() ? &root : jsonFind(root, file.c_str());
        if (!arr || !arr->is_array()) {
            pk.error = file + ".json: expected a list (either the whole file or under \"" + file + "\")";
            return false;
        }
        size_t index = 0;
        for (const json& o : *arr) {
            ++index;
            if (!o.is_object()) {
                warn(file + ".json #" + std::to_string(index) + ": not an object, skipped");
                continue;
            }
            const std::string name = trimmed(jsonStr(o, "name"));
            if (name.empty()) {
                warn(file + ".json #" + std::to_string(index) + ": has no \"name\", skipped");
                continue;
            }
            fn(o, name);
        }
        return true;
    }

    // ---- card kinds -------------------------------------------------------------------------------

    void spell(const json& o, const std::string& name) {
        Entry& e = push(Kind::Spell, o, name);
        const std::string school = jsonStr(o, "school", "General Magic");
        const bool trick = jsonBool(o, "trick");
        e.subtitle = school + (trick ? " · magic trick" : " · spell");
        addField(e.fields, "Rank", jsonStr(o, "rank"));
        addField(e.fields, "Prerequisite", jsonStr(o, "prerequisite"));
        addField(e.fields, "Requirement", jsonStr(o, "requirement"));
        addField(e.fields, "Casting time", jsonStr(o, "casting_time"));
        addField(e.fields, "Range", jsonStr(o, "range"));
        addField(e.fields, "Duration", jsonStr(o, "duration"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["school"] = school;
        e.props["trick"] = trick ? "1" : "0";
        e.props["rank"] = jsonStr(o, "rank");
    }

    void ability(const json& o, const std::string& name) {
        Entry& e = push(Kind::Ability, o, name);
        std::string type = lowerCopy(jsonStr(o, "type", "heroic"));
        if (type != "kin") type = "heroic";
        const std::string kin = jsonStr(o, "kin");
        e.subtitle = type == "heroic" ? "Heroic ability" : "Innate ability" + (kin.empty() ? "" : " · " + kin);
        addField(e.fields, "Requirement", jsonStr(o, "requirement"));
        addField(e.fields, "Willpower points", jsonStr(o, "wp_cost"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["type"] = type;
        e.props["kin"] = kin;
        e.props["wp_cost"] = jsonStr(o, "wp_cost");
    }

    void skill(const json& o, const std::string& name) {
        Entry& e = push(Kind::Skill, o, name);
        std::string category = lowerCopy(jsonStr(o, "category", "core"));
        std::string attribute = jsonStr(o, "attribute");
        for (char& c : attribute) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        e.subtitle = category + " skill";
        addField(e.fields, "Attribute", attribute);
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["attribute"] = attribute;
        e.props["category"] = category;
    }

    void kin(const json& o, const std::string& name) {
        Entry& e = push(Kind::Kin, o, name);
        e.subtitle = "Kin";
        const int movement = jsonInt(o, "movement");
        addField(e.fields, "Movement", movement > 0 ? std::to_string(movement) : "");
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["movement"] = movement > 0 ? std::to_string(movement) : "";
        e.lists["innate_abilities"] = jsonStrings(o, "innate_abilities");
        e.lists["names"] = jsonStrings(o, "names");
    }

    void profession(const json& o, const std::string& name) {
        Entry& e = push(Kind::Profession, o, name);
        e.subtitle = "Profession";
        std::string key = jsonStr(o, "key_attribute");
        for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        addField(e.fields, "Key attribute", key);
        e.props["key_attribute"] = key;
        e.lists["skills"] = jsonStrings(o, "skills");
        if (!e.list("skills").empty()) addField(e.fields, "Skills", joinList(e.list("skills"), ", "));
        if (const json* bs = jsonFind(o, "skills_by_school"); bs && bs->is_object()) {
            std::vector<std::string> schools;
            for (auto it = bs->begin(); it != bs->end(); ++it) {
                schools.push_back(it.key());
                e.lists["skills." + it.key()] = jsonStrings(*bs, it.key().c_str());
                addField(e.fields, it.key() + " skills", joinList(e.lists["skills." + it.key()], ", "));
            }
            e.lists["schools"] = schools;
        }
        std::vector<std::string> heroic = jsonStrings(o, "heroic_abilities");
        if (heroic.empty()) {                       // "heroic_ability": "A, B or C" as printed
            std::string one = jsonStr(o, "heroic_ability");
            size_t pos;
            while ((pos = one.find(" or ")) != std::string::npos) one.replace(pos, 4, ", ");
            for (size_t start = 0; start < one.size();) {
                size_t end = one.find(',', start);
                if (end == std::string::npos) end = one.size();
                const std::string part = trimmed(one.substr(start, end - start));
                if (!part.empty()) heroic.push_back(part);
                start = end + 1;
            }
        }
        e.lists["heroic_abilities"] = heroic;
        addField(e.fields, "Heroic ability", heroic.empty() ? jsonStr(o, "heroic_ability") : joinList(heroic, " or "));
        e.lists["starting_gear"] = jsonStrings(o, "starting_gear");
        e.lists["nicknames"] = jsonStrings(o, "nicknames");
        if (const json* mg = jsonFind(o, "magic"); mg && mg->is_object()) {
            e.props["magic"] = "1";
            e.props["magic.spells"] = std::to_string(jsonInt(*mg, "spells", 3));
            e.props["magic.tricks"] = std::to_string(jsonInt(*mg, "tricks", 3));
            e.props["magic.spell_rank"] = std::to_string(jsonInt(*mg, "spell_rank", 1));
        }
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
    }

    void weapon(const json& o, const std::string& name) {
        Entry& e = push(Kind::Weapon, o, name);
        const std::string kind = lowerCopy(jsonStr(o, "kind", "melee"));
        e.subtitle = kind == "ranged" ? "Ranged weapon" : "Melee weapon";
        addField(e.fields, "Grip", jsonStr(o, "grip"));
        addField(e.fields, "STR requirement", jsonStr(o, "str_req"));
        addField(e.fields, "Range", jsonStr(o, "range"));
        addField(e.fields, "Damage", jsonStr(o, "damage"));
        addField(e.fields, "Durability", jsonStr(o, "durability"));
        addField(e.fields, "Cost", jsonStr(o, "cost"));
        addField(e.fields, "Supply", jsonStr(o, "supply"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "features");
        for (const char* p : {"grip", "range", "damage", "durability", "str_req"}) e.props[p] = jsonStr(o, p);
        e.props["kind"] = kind;
    }

    void armor(const json& o, const std::string& name) {
        Entry& e = push(Kind::Armor, o, name);
        const std::string slot = lowerCopy(jsonStr(o, "slot", "armor")) == "helmet" ? "helmet" : "armor";
        e.subtitle = slot == "helmet" ? "Helmet" : "Armor";
        addField(e.fields, "Armor rating", jsonStr(o, "armor_rating"));
        addField(e.fields, "Cost", jsonStr(o, "cost"));
        addField(e.fields, "Supply", jsonStr(o, "supply"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "effect");
        e.props["slot"] = slot;
        e.props["armor_rating"] = jsonStr(o, "armor_rating");
    }

    void gear(const json& o, const std::string& name) {
        Entry& e = push(Kind::Gear, o, name);
        const std::string category = jsonStr(o, "category");
        e.subtitle = category.empty() ? "Gear" : "Gear · " + category;
        addField(e.fields, "Cost", jsonStr(o, "cost"));
        addField(e.fields, "Supply", jsonStr(o, "supply"));
        addField(e.fields, "Weight", jsonStr(o, "weight"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "effect");
        e.props["category"] = category;
    }

    // ---- creatures ---------------------------------------------------------------------------------

    static void statFields(const json& b, std::vector<Field>& out) {
        const json* f = jsonFind(b, "fields");
        if (!f) return;
        if (f->is_object()) {
            for (auto it = f->begin(); it != f->end(); ++it) addField(out, it.key(), jsonText(it.value()));
        } else if (f->is_array()) {
            for (const json& e : *f)
                if (e.is_object()) addField(out, jsonStr(e, "label"), jsonStr(e, "value"));
                else if (e.is_array() && e.size() >= 2) addField(out, jsonText(e[0]), jsonText(e[1]));
        }
    }

    void creature(const json& o, const std::string& name) {
        Monster m;
        m.key = makeKey(Kind::Monster, o, name);
        m.sourceId = sourceFor(o, "creature \"" + name + "\"");
        m.name = name;
        m.kind = lowerCopy(jsonStr(o, "kind", "monster"));
        if (m.kind != "npc" && m.kind != "animal") m.kind = "monster";
        m.category = jsonStr(o, "category");
        m.description = jsonStr(o, "description");
        m.quote = jsonStr(o, "quote");
        m.randomEncounter = jsonStr(o, "random_encounter");
        m.adventureSeed = jsonStr(o, "adventure_seed");
        m.statsRef = jsonStr(o, "stats_ref");
        m.attackDice = normalizeDice(jsonStr(o, "attack_dice"));
        pageInfo(o, m.sourceId, m.ref, m.pageNote);
        const std::string image = jsonStr(o, "image");
        if (!image.empty()) {
            if (!safeRelPath(image)) warn("creature \"" + name + "\": image path \"" + image + "\" must stay inside the pack folder");
            else if (!isFile(pk.dir + "/" + image)) warn("creature \"" + name + "\": image \"" + image + "\" not found");
            else {
                m.image = pk.dir + "/" + image;
                const int ip = jsonInt(o, "image_page");
                const SourceInfo* si = st.source(m.sourceId);
                if (si && si->book && ip > 0) m.imageRef = {m.sourceId, ip, 0};
            }
        }
        if (const json* sb = jsonFind(o, "statblocks"); sb && sb->is_array()) {
            for (const json& b : *sb) {
                if (!b.is_object()) continue;
                StatBlock s;
                s.variant = jsonStr(b, "variant");
                statFields(b, s.fields);
                std::string note;
                pageInfo(b, m.sourceId, s.ref, note);
                m.blocks.push_back(std::move(s));
            }
        } else if (jsonFind(o, "fields")) {          // shorthand: a single stat block written directly
            StatBlock s;
            statFields(o, s.fields);
            m.blocks.push_back(std::move(s));
        }
        if (const json* at = jsonFind(o, "attacks"); at && at->is_array()) {
            int row = 0;
            for (const json& a : *at) {
                if (!a.is_object()) continue;
                ++row;
                Attack k;
                k.name = jsonStr(a, "name");
                k.text = jsonStr(a, "text");
                if (k.text.empty()) k.text = jsonStr(a, "description");
                if (!k.name.empty() && k.text.rfind(k.name, 0) != 0) k.text = k.name + " " + k.text;
                k.rollText = jsonStr(a, "roll");
                if (k.rollText.empty() && jsonFind(a, "roll_min")) {
                    k.rollMin = jsonInt(a, "roll_min");
                    k.rollMax = jsonInt(a, "roll_max", k.rollMin);
                    k.rollText = k.rollMin == k.rollMax ? std::to_string(k.rollMin)
                                                        : std::to_string(k.rollMin) + "-" + std::to_string(k.rollMax);
                } else if (!parseRoll(k.rollText, k.rollMin, k.rollMax)) {
                    k.rollMin = k.rollMax = row;         // no roll given: rows are numbered in order
                    k.rollText = std::to_string(row);
                }
                std::string note;
                pageInfo(a, m.sourceId, k.ref, note);
                m.attacks.push_back(std::move(k));
            }
        }
        if (const json* ab = jsonFind(o, "abilities"); ab && ab->is_array())
            for (const json& a : *ab)
                if (a.is_object() && !jsonStr(a, "name").empty()) {
                    const std::string kind = jsonStr(a, "kind") == "pc_ability" ? "pc_ability" : "ability";
                    m.abilities.push_back({jsonStr(a, "name"), kind, jsonStr(a, "text", jsonStr(a, "description"))});
                }
        for (const std::string& t : jsonStrings(o, "tables")) m.tables.push_back({0, t});
        m.id = static_cast<int>(st.monsters_.size()) + 1;
        st.byKey_[static_cast<int>(Kind::Monster)][m.key] = m.id;
        st.monsters_.push_back(std::move(m));
        ++pk.counts[static_cast<int>(Kind::Monster)];
    }

    // ---- tables ------------------------------------------------------------------------------------

    void table(const json& o, const std::string& name) {
        DataTable t;
        t.key = makeKey(Kind::Table, o, name);
        t.title = name;
        t.sourceId = sourceFor(o, "table \"" + name + "\"");
        t.role = lowerCopy(jsonStr(o, "role"));
        t.browse = jsonBool(o, "browse", true);
        t.dice = normalizeDice(jsonStr(o, "dice"));
        t.columns = jsonStrings(o, "columns");
        pageInfo(o, t.sourceId, t.ref, t.pageNote);
        const json* rows = jsonFind(o, "rows");
        int index = 0;
        if (rows && rows->is_array())
            for (const json& r : *rows) {
                TableRow row;
                ++index;
                if (r.is_object()) {
                    row.cells = jsonStrings(r, "cells");
                    row.rollText = jsonStr(r, "roll");
                } else if (r.is_array()) {
                    for (const json& c : r) row.cells.push_back(jsonText(c));
                } else {
                    row.cells.push_back(jsonText(r));
                }
                if (!t.dice.empty()) {
                    if (row.rollText.empty() || !parseRoll(row.rollText, row.rollMin, row.rollMax)) {
                        row.rollMin = row.rollMax = index;
                        row.rollText = std::to_string(index);
                    }
                }
                t.rows.push_back(std::move(row));
            }
        if (t.columns.empty() && !t.rows.empty()) t.columns.assign(t.rows.front().cells.size(), "");
        t.id = kPackTableBase + static_cast<int>(st.tables_.size()) + 1;
        st.byKey_[static_cast<int>(Kind::Table)][t.key] = t.id;
        st.tables_.push_back(std::move(t));
        ++pk.counts[static_cast<int>(Kind::Table)];
    }

    // ------------------------------------------------------------------------------------------------

    void run() {
        setupSources();
        struct Step {
            const char* file;
            void (Loader::*fn)(const json&, const std::string&);
        };
        const Step steps[] = {{"spells", &Loader::spell},   {"abilities", &Loader::ability}, {"skills", &Loader::skill},
                              {"kin", &Loader::kin},        {"professions", &Loader::profession},
                              {"weapons", &Loader::weapon}, {"armor", &Loader::armor},     {"gear", &Loader::gear},
                              {"tables", &Loader::table},   {"creatures", &Loader::creature}};
        for (const Step& s : steps) {
            if (!eachObject(s.file, [&](const json& o, const std::string& n) { (this->*s.fn)(o, n); })) return;
        }
        pk.loaded = true;
    }
};

void ContentStore::clear() {
    packs_.clear();
    sources_.clear();
    for (auto& v : entries_) v.clear();
    for (auto& m : byKey_) m.clear();
    monsters_.clear();
    tables_.clear();
    if (index_) sqlite3_close(index_);
    index_ = nullptr;
}

ContentStore::~ContentStore() { clear(); }

void ContentStore::load(Database& db, const std::vector<PackSpec>& specs) {
    clear();
    for (const Source& s : db.sources()) {
        SourceInfo si;
        si.id = s.id;
        si.key = s.key;
        si.packId = "core";
        si.title = s.title;
        si.label = s.key;
        if (!si.label.empty()) si.label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(si.label[0])));
        si.book = true;
        si.color = s.key == "rulebook" ? 0x4DC7B3u : s.key == "bestiary" ? 0xEE9E40u : s.key == "adventure" ? 0xB38CF2u : colorFor(s.key);
        sources_.push_back(si);
    }
    std::set<std::string> seen;
    for (const PackSpec& spec : specs) {
        PackInfo pk;
        pk.core = spec.core;
        pk.enabled = spec.enabled;
        if (!readManifest(spec.dir, pk)) {
            if (pk.id.empty()) pk.id = spec.dir;
            if (pk.name.empty()) pk.name = pk.id;
            packs_.push_back(std::move(pk));
            continue;
        }
        if (!seen.insert(pk.id).second) {
            pk.error = "another pack already uses the id \"" + pk.id + "\"";
            packs_.push_back(std::move(pk));
            continue;
        }
        if (spec.enabled) {
            // load into the shared vectors; if the pack turns out to be broken, roll everything it added back
            size_t before[kKindCount];
            for (int k = 0; k < kKindCount; ++k) before[k] = entries_[k].size();
            const size_t monBefore = monsters_.size(), tabBefore = tables_.size(), srcBefore = sources_.size();
            std::string text, err;
            json manifest;
            readTextFile(spec.dir + "/manifest.json", text, &err);
            jsonParse(text, manifest, &err);
            Loader loader(*this, pk, manifest);
            loader.run();
            if (!pk.loaded) {
                for (int k = 0; k < kKindCount; ++k) {
                    entries_[k].resize(before[k]);
                    pk.counts[k] = 0;
                }
                monsters_.resize(monBefore);
                tables_.resize(tabBefore);
                sources_.resize(srcBefore);
                pk.sourceIds.clear();
                const std::string prefix = pk.id + "/";
                for (auto& m : byKey_)
                    for (auto it = m.begin(); it != m.end();) it = it->first.starts_with(prefix) ? m.erase(it) : std::next(it);
            }
        }
        packs_.push_back(std::move(pk));
    }
    resolveLinks(db);
    buildIndex();
}

// Things that need every pack loaded: creature -> related book tables, kin -> their innate abilities' text.
void ContentStore::resolveLinks(Database& db) {
    std::map<std::string, int> dbTables;
    for (const ListItem& t : db.listTables()) dbTables.emplace(std::to_string(t.sourceId) + "|" + t.name, t.id);
    for (Monster& m : monsters_) {
        std::vector<TableRef> resolved;
        for (const TableRef& t : m.tables) {
            auto it = dbTables.find(std::to_string(m.sourceId) + "|" + t.title);
            if (it != dbTables.end()) {
                resolved.push_back({it->second, t.title});
                continue;
            }
            for (const DataTable& pt : tables_)
                if (lowerCopy(pt.title) == lowerCopy(t.title)) {
                    resolved.push_back({pt.id, pt.title});
                    break;
                }
        }
        m.tables = std::move(resolved);
    }
    for (Entry& k : entries_[static_cast<int>(Kind::Kin)]) {
        for (const std::string& name : k.list("innate_abilities")) {
            const Entry* a = findByName(Kind::Ability, name);
            if (!a) continue;
            const std::string wp = a->prop("wp_cost");
            const std::string head = a->title + (wp.empty() ? "" : " (WP " + wp + ")");
            k.fields.push_back({"Innate ability", head});
            k.body += (k.body.empty() ? "" : "\n\n") + head + "\n" + a->body;
        }
    }
}

// ------------------------------------------------------------------------------------- lookups

const PackInfo* ContentStore::pack(const std::string& id) const {
    for (const PackInfo& p : packs_)
        if (p.id == id) return &p;
    return nullptr;
}

const SourceInfo* ContentStore::source(int id) const {
    for (const SourceInfo& s : sources_)
        if (s.id == id) return &s;
    return nullptr;
}

int ContentStore::sourceId(const std::string& packId, const std::string& key) const {
    for (const SourceInfo& s : sources_)
        if (s.packId == packId && s.key == key) return s.id;
    return 0;
}

const std::vector<Entry>& ContentStore::entries(Kind k) const { return entries_[static_cast<int>(k)]; }

const Entry* ContentStore::entry(Kind k, int id) const {
    const auto& v = entries_[static_cast<int>(k)];
    return id >= 1 && id <= static_cast<int>(v.size()) ? &v[static_cast<size_t>(id) - 1] : nullptr;
}

const Entry* ContentStore::findByName(Kind k, const std::string& name) const {
    const std::string want = lowerCopy(trimmed(name));
    for (const Entry& e : entries_[static_cast<int>(k)])
        if (lowerCopy(e.title) == want) return &e;
    return nullptr;
}

const Monster* ContentStore::monster(int id) const {
    return id >= 1 && id <= static_cast<int>(monsters_.size()) ? &monsters_[static_cast<size_t>(id) - 1] : nullptr;
}

std::vector<ListItem> ContentStore::listMonsters() const {
    std::vector<ListItem> out;
    for (const Monster& m : monsters_) {
        ListItem it;
        it.id = m.id;
        it.kind = Kind::Monster;
        it.name = m.name;
        it.sub = m.category;
        if (m.kind == "npc") it.sub = it.sub.empty() ? "NPC" : "NPC · " + it.sub;
        else if (m.kind == "animal") it.sub = "Animal";
        it.sourceId = m.sourceId;
        it.page = m.ref.page;
        out.push_back(std::move(it));
    }
    std::ranges::stable_sort(out, [](const ListItem& a, const ListItem& b) {
        const std::string la = lowerCopy(a.name), lb = lowerCopy(b.name);
        return la != lb ? la < lb : a.sourceId < b.sourceId;
    });
    return out;
}

std::vector<ListItem> ContentStore::creatureVersions(const std::string& name, int exceptId) const {
    std::vector<ListItem> out;
    const std::string want = lowerCopy(name);
    for (const Monster& m : monsters_)
        if (m.id != exceptId && lowerCopy(m.name) == want) out.push_back({m.id, Kind::Monster, m.name, m.kind, m.sourceId, m.ref.page});
    std::ranges::stable_sort(out, [](const ListItem& a, const ListItem& b) { return a.sourceId < b.sourceId; });
    return out;
}

std::vector<ListItem> ContentStore::creaturesOnPrintedPage(int sourceId, int printedPage) const {
    std::vector<ListItem> out;
    auto rank = [](const std::string& k) { return k == "monster" ? 0 : k == "npc" ? 1 : 2; };
    std::vector<const Monster*> found;
    for (const Monster& m : monsters_)
        if (m.sourceId == sourceId && m.ref.printed == printedPage) found.push_back(&m);
    std::ranges::stable_sort(found, [&](const Monster* a, const Monster* b) { return rank(a->kind) < rank(b->kind); });
    for (const Monster* m : found) {
        if (out.size() >= 4) break;
        out.push_back({m->id, Kind::Monster, m->name, m->kind, m->sourceId, m->ref.page});
    }
    return out;
}

const DataTable* ContentStore::packTable(int id) const {
    const int i = id - kPackTableBase;
    return i >= 1 && i <= static_cast<int>(tables_.size()) ? &tables_[static_cast<size_t>(i) - 1] : nullptr;
}

std::vector<const DataTable*> ContentStore::tablesByRole(const std::string& role) const {
    std::vector<const DataTable*> out;
    for (const DataTable& t : tables_)
        if (t.role == role) out.push_back(&t);
    return out;
}

const DataTable* ContentStore::tableByRole(const std::string& role) const {
    auto all = tablesByRole(role);
    return all.empty() ? nullptr : all.front();
}

std::string ContentStore::keyOf(Kind k, int id) const {
    if (k == Kind::Table && id < kPackTableBase) return "#" + std::to_string(id);
    if (k == Kind::Monster) {
        const Monster* m = monster(id);
        return m ? m->key : std::string();
    }
    if (k == Kind::Table) {
        const DataTable* t = packTable(id);
        return t ? t->key : std::string();
    }
    const Entry* e = entry(k, id);
    return e ? e->key : std::string();
}

int ContentStore::idByKey(Kind k, const std::string& key) const {
    if (!key.empty() && key[0] == '#') return std::atoi(key.c_str() + 1);
    const auto& m = byKey_[static_cast<int>(k)];
    auto it = m.find(key);
    return it == m.end() ? 0 : it->second;
}

std::string ContentStore::titleOf(Kind k, int id) const {
    if (k == Kind::Monster) {
        const Monster* m = monster(id);
        return m ? m->name : std::string();
    }
    if (k == Kind::Table) {
        const DataTable* t = packTable(id);
        return t ? t->title : std::string();
    }
    const Entry* e = entry(k, id);
    return e ? e->title : std::string();
}

int ContentStore::count(Kind k) const {
    if (k == Kind::Monster) return static_cast<int>(monsters_.size());
    if (k == Kind::Table) return static_cast<int>(tables_.size());
    return static_cast<int>(entries_[static_cast<int>(k)].size());
}

// ------------------------------------------------------------------------------------- search

void ContentStore::buildIndex() {
    if (sqlite3_open(":memory:", &index_) != SQLITE_OK) {
        index_ = nullptr;
        return;
    }
    sqlite3_exec(index_,
                 "CREATE VIRTUAL TABLE idx USING fts5(kind, ref_id UNINDEXED, title, body, source UNINDEXED, page UNINDEXED,"
                 " tokenize = 'porter unicode61 remove_diacritics 2');",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(index_, "BEGIN", nullptr, nullptr, nullptr);
    Stmt ins(index_, "INSERT INTO idx(kind, ref_id, title, body, source, page) VALUES (?,?,?,?,?,?)");
    auto add = [&](Kind k, int id, const std::string& title, const std::string& body, int source, int page) {
        ins.bind(1, std::string(kindKey(k))).bind(2, id).bind(3, title).bind(4, body).bind(5, source).bind(6, page);
        ins.run();
        ins.reset();
    };
    for (int k = 0; k < kKindCount; ++k)
        for (const Entry& e : entries_[k]) {
            std::string body = e.subtitle + "\n";
            for (const Field& f : e.fields) body += f.label + ": " + f.value + "\n";
            add(e.kind, e.id, e.title, body + e.body, e.sourceId, e.ref.page);
        }
    for (const Monster& m : monsters_) {
        std::string body = m.category + "\n" + m.quote + "\n" + m.description + "\n";
        for (const StatBlock& b : m.blocks)
            for (const Field& f : b.fields) body += f.label + ": " + f.value + "\n";
        for (const Attack& a : m.attacks) body += a.text + "\n";
        for (const NamedText& a : m.abilities) body += a.name + " " + a.text + "\n";
        add(Kind::Monster, m.id, m.name, body + m.randomEncounter + "\n" + m.adventureSeed, m.sourceId, m.ref.page);
    }
    for (const DataTable& t : tables_) {
        if (!t.browse) continue;
        std::string body;
        for (const TableRow& r : t.rows) {
            body += r.rollText + " ";
            for (const std::string& c : r.cells) body += c + " | ";
            body += "\n";
        }
        add(Kind::Table, t.id, t.title, body, t.sourceId, t.ref.page);
    }
    sqlite3_exec(index_, "COMMIT", nullptr, nullptr, nullptr);
}

std::vector<Hit> ContentStore::search(const std::string& userText, int limit, const std::vector<Kind>& only) const {
    std::vector<Hit> out;
    const std::string match = ftsMatch(userText);
    if (match.empty() || !index_) return out;
    std::string kinds;                                   // kind names are ours, never user input
    for (Kind k : only) kinds += std::string(kinds.empty() ? "" : ",") + "'" + kindKey(k) + "'";
    const std::string sql = std::string("SELECT kind, ref_id, title, snippet(idx, 3, '\x01', '\x02', ' … ', 14), source, page, "
                                        "lower(title) = lower(?3) FROM idx WHERE idx MATCH ?1 ") +
                            (kinds.empty() ? "" : "AND kind IN (" + kinds + ") ") +
                            "ORDER BY 7 DESC, bm25(idx, 0.0, 0.0, 12.0, 1.0, 0.0, 0.0) LIMIT ?2";
    Stmt q(index_, sql.c_str());
    if (!q.ok()) return out;
    q.bind(1, match).bind(2, limit).bind(3, trimmed(userText));
    while (q.step()) {
        Hit h;
        if (!kindFromKey(q.s(0), h.kind)) continue;
        h.id = q.i(1);
        h.title = q.s(2);
        h.snippet = q.s(3);
        h.sourceId = q.i(4);
        h.page = q.i(5);
        h.exact = q.i(6) != 0;
        out.push_back(std::move(h));
    }
    return out;
}

std::vector<Hit> mergeHits(std::vector<Hit> content, std::vector<Hit> book, size_t limit) {
    std::vector<Hit> out;
    for (auto* list : {&content, &book})
        for (Hit& h : *list)
            if (h.exact) out.push_back(h);
    for (auto* list : {&content, &book})
        for (Hit& h : *list)
            if (!h.exact) out.push_back(h);
    if (out.size() > limit) out.resize(limit);
    return out;
}

}  // namespace gm
