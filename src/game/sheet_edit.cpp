#include "game/sheet_edit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "game/encounter.h"
#include "parsing/fsutil.h"

namespace gm::sheet {
namespace {

const std::set<std::string, std::less<>> kPlayerKeys = {"name",     "nickname", "age",       "weakness", "memento", "appearance", "notes",       "attributes", "hp",
                                                        "wp",       "hp_bonus", "wp_bonus",  "conditions", "skills", "abilities",  "spells",      "weapons",
                                                        "armor",    "helmet",   "inventory", "tiny_items", "coins"};
const std::set<std::string, std::less<>> kMeta = {"format", "id", "revision", "created_at", "updated_at"};
const std::set<std::string, std::less<>> kMergedObjects = {"attributes", "coins", "reviews"};

bool isInt(const json& v, long long lo, long long hi) {
    if (!v.is_number()) return false;
    const double d = v.get<double>();
    return d == std::floor(d) && d >= static_cast<double>(lo) && d <= static_cast<double>(hi);
}

bool isText(const json& v, size_t max) { return v.is_string() && v.get_ref<const std::string&>().size() <= max; }

std::string validateItem(const json& v, const char* what) {
    if (!v.is_object()) return std::string(what) + " must be an object";
    if (!v.contains("name") || !isText(v["name"], 80)) return std::string(what) + " needs a name (80 letters at most)";
    if (v.contains("key") && !isText(v["key"], 120)) return std::string(what) + " has a bad key";
    if (v.contains("count") && !isInt(v["count"], 1, 999)) return std::string(what) + " count must be 1 to 999";
    if (v.contains("note") && !isText(v["note"], 200)) return std::string(what) + " note is too long";
    return {};
}

std::string validateRefs(const json& v, const char* what) {
    if (!v.is_array() || v.size() > 80) return std::string(what) + " must be a list of at most 80";
    for (const json& r : v) {
        if (r.is_string()) {
            if (!isText(r, 80)) return std::string(what) + " has a name that is too long";
        } else if (!r.is_object() || !r.contains("name") || !isText(r["name"], 80) || (r.contains("key") && !isText(r["key"], 120))) {
            return std::string(what) + " has a bad entry";
        }
    }
    return {};
}

// The problem with a value, or "" if it is fine.
std::string validate(const std::string& key, const json& v) {
    static const std::map<std::string, size_t, std::less<>> texts = {{"name", 80}, {"nickname", 80}, {"weakness", 400}, {"memento", 400}, {"appearance", 1500}, {"notes", 8000}};
    if (const auto t = texts.find(key); t != texts.end()) return isText(v, t->second) ? "" : key + " must be text of at most " + std::to_string(t->second) + " letters";
    if (key == "age") return v.is_string() && (v == "young" || v == "adult" || v == "old") ? "" : "age must be young, adult or old";
    if (key == "hp" || key == "wp") return isInt(v, -99, 999) ? "" : key + " must be a whole number";
    if (key == "hp_bonus" || key == "wp_bonus") return isInt(v, -99, 99) ? "" : key + " must be a whole number";
    if (key == "attributes") {
        if (!v.is_object()) return "attributes must be an object";
        for (auto it = v.begin(); it != v.end(); ++it)
            if (attrIndex(it.key()) < 0 || !isInt(it.value(), 0, 99)) return "attributes has a bad value for " + it.key();
        return {};
    }
    if (key == "coins") {
        if (!v.is_object()) return "coins must be an object";
        for (auto it = v.begin(); it != v.end(); ++it)
            if ((it.key() != "gold" && it.key() != "silver" && it.key() != "copper") || !isInt(it.value(), 0, 99999)) return "coins has a bad value for " + it.key();
        return {};
    }
    if (key == "conditions") {
        if (!v.is_array() || v.size() > 6) return "conditions must be a list";
        for (const json& c : v) {
            if (!c.is_string()) return "conditions must be names";
            if (std::none_of(std::begin(kConditions), std::end(kConditions), [&](const ConditionInfo& info) { return c == info.name; })) return "unknown condition";
        }
        return {};
    }
    if (key == "skills") {
        if (!v.is_array() || v.size() > 150) return "skills must be a list of at most 150";
        for (const json& s : v) {
            if (!s.is_object() || !s.contains("name") || !isText(s["name"], 60) || s["name"].get_ref<const std::string&>().empty()) return "a skill needs a name";
            if (s.contains("key") && !isText(s["key"], 120)) return "a skill has a bad key";
            if (s.contains("attribute") && (!s["attribute"].is_string() || (s["attribute"] != "" && attrIndex(s["attribute"].get<std::string>()) < 0))) return "a skill has a bad attribute";
            if (s.contains("level") && !isInt(s["level"], 0, 99)) return "a skill level must be a whole number";
            if ((s.contains("trained") && !s["trained"].is_boolean()) || (s.contains("marked") && !s["marked"].is_boolean())) return "a skill has a bad flag";
        }
        return {};
    }
    if (key == "locked") return v.is_boolean() ? "" : "locked must be true or false";                  // (only the GM's own path gets this far)
    if (key == "reviews") {
        if (!v.is_object()) return "reviews must be an object";
        for (auto it = v.begin(); it != v.end(); ++it)
            if (!it.value().is_null() && (!it.value().is_object() || !isText(it.value().value("status", json(nullptr)), 20) || !isText(it.value().value("value", json(nullptr)), 40))) return "a ruling is malformed";
        return {};
    }
    if (key == "abilities") return validateRefs(v, "abilities");
    if (key == "spells") return validateRefs(v, "spells");
    if (key == "weapons" || key == "inventory") {
        if (!v.is_array() || v.size() > 100) return key + " must be a list of at most 100";
        for (const json& it : v)
            if (const std::string e = validateItem(it, key.c_str()); !e.empty()) return e;
        return {};
    }
    if (key == "armor" || key == "helmet") return v.is_null() ? "" : validateItem(v, key.c_str());
    if (key == "tiny_items") {
        if (!v.is_array() || v.size() > 100) return "tiny_items must be a list of at most 100";
        return std::ranges::all_of(v, [](const json& t) { return isText(t, 120); }) ? "" : "a tiny item is too long";
    }
    return "unknown field " + key;
}

const char* labelOf(const std::string& key) {
    static const std::map<std::string, const char*, std::less<>> labels = {
        {"hp", "HP"}, {"wp", "WP"}, {"hp_bonus", "max HP bonus"}, {"wp_bonus", "max WP bonus"}, {"name", "name"}, {"nickname", "nickname"}, {"age", "age"},
        {"weakness", "weakness"}, {"memento", "memento"}, {"appearance", "appearance"}, {"notes", "notes"}, {"armor", "armor"}, {"helmet", "helmet"}};
    const auto it = labels.find(key);
    return it == labels.end() ? key.c_str() : it->second;
}

std::string text(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_null()) return "none";
    if (v.is_object() && v.contains("name")) return v["name"].get<std::string>();
    return v.dump();
}

// name -> count, for lists of things that have names (items, abilities, tiny items)
std::map<std::string, int> counted(const json& list) {
    std::map<std::string, int> out;
    if (!list.is_array()) return out;
    for (const json& e : list) {
        const std::string name = e.is_string() ? e.get<std::string>() : e.value("name", std::string());
        out[name] += e.is_object() && e.contains("count") && e["count"].is_number_integer() ? e["count"].get<int>() : 1;
    }
    return out;
}

void joinInto(std::string& out, const std::string& part) {
    if (part.empty()) return;
    out += out.empty() ? part : "; " + part;
}

std::string describeKey(const std::string& key, const json& before, const json& after) {
    if (key == "attributes" || key == "coins") {
        std::string out;
        for (auto it = after.begin(); it != after.end(); ++it) {
            std::string name = it.key();
            if (key == "coins") name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            joinInto(out, name + " " + (before.contains(it.key()) ? text(before[it.key()]) : "?") + " → " + text(it.value()));
        }
        return out;
    }
    if (key == "conditions") {
        std::string out;
        for (const json& c : after)
            if (!std::ranges::contains(before, c)) joinInto(out, "+" + text(c));
        for (const json& c : before)
            if (!std::ranges::contains(after, c)) joinInto(out, "-" + text(c));
        return out;
    }
    if (key == "skills") {
        std::map<std::string, json> was;
        for (const json& s : before) was[s.value("name", std::string())] = s;
        std::string out;
        for (const json& s : after) {
            const std::string name = s.value("name", std::string());
            const auto old = was.find(name);
            if (old == was.end()) {
                joinInto(out, "+" + name + " " + std::to_string(s.value("level", 0)));
                continue;
            }
            const json& o = old->second;
            if (o.value("level", 0) != s.value("level", 0)) joinInto(out, name + " " + std::to_string(o.value("level", 0)) + " → " + std::to_string(s.value("level", 0)));
            if (o.value("trained", false) != s.value("trained", false)) joinInto(out, name + (s.value("trained", false) ? " trained" : " untrained"));
            if (o.value("marked", false) != s.value("marked", false)) joinInto(out, name + (s.value("marked", false) ? " marked" : " mark removed"));
            was.erase(old);
        }
        for (const auto& [name, s] : was) joinInto(out, "-" + name);
        return out;
    }
    if (key == "abilities" || key == "spells" || key == "weapons" || key == "inventory" || key == "tiny_items") {
        const auto was = counted(before), now = counted(after);
        std::string out;
        for (const auto& [name, n] : now) {
            const auto old = was.find(name);
            if (old == was.end()) joinInto(out, "+" + name + (n > 1 ? " ×" + std::to_string(n) : ""));
            else if (old->second != n) joinInto(out, name + " ×" + std::to_string(old->second) + " → ×" + std::to_string(n));
        }
        for (const auto& [name, n] : was)
            if (!now.contains(name)) joinInto(out, "-" + name);
        return out;
    }
    const std::string label = labelOf(key);
    if (after.is_string() && after.get_ref<const std::string&>().size() > 30) return label + " edited";
    return label + " " + text(before) + " → " + text(after);
}

}  // namespace

// ------------------------------------------------------------------------------------------------- sets

bool playerMayEdit(std::string_view key) { return kPlayerKeys.contains(key); }

Applied applySet(json& doc, const json& set, bool asPlayer) {
    Applied result;
    if (!set.is_object()) return {false, "the changes must be an object", {}};
    for (auto it = set.begin(); it != set.end(); ++it) {
        if (asPlayer && !playerMayEdit(it.key())) return {false, "you cannot change " + it.key(), {}};
        if (const std::string problem = validate(it.key(), it.value()); !problem.empty()) return {false, problem, {}};
    }
    for (auto it = set.begin(); it != set.end(); ++it) {
        const json before = doc.contains(it.key()) ? doc[it.key()] : json(nullptr);
        json one = json::object();
        one[it.key()] = it.value();
        merge(doc, one);
        if (doc[it.key()] != before) result.keys.push_back(it.key());
    }
    return result;
}

void merge(json& doc, const json& set) {
    for (auto it = set.begin(); it != set.end(); ++it) {
        if (kMergedObjects.contains(it.key()) && it.value().is_object()) {
            if (!doc.contains(it.key()) || !doc[it.key()].is_object()) doc[it.key()] = json::object();
            for (auto sub = it.value().begin(); sub != it.value().end(); ++sub) {
                if (sub.value().is_null()) doc[it.key()].erase(sub.key());
                else doc[it.key()][sub.key()] = sub.value();
            }
            if (it.key() == "reviews" && doc[it.key()].empty()) doc.erase(it.key());
        } else {
            doc[it.key()] = it.value();
        }
    }
}

json diff(const json& before, const json& after) {
    json set = json::object();
    std::set<std::string> keys;
    for (const json* d : {&before, &after})
        for (auto it = d->begin(); it != d->end(); ++it)
            if (!kMeta.contains(it.key())) keys.insert(it.key());
    for (const std::string& key : keys) {
        const json* b = before.contains(key) ? &before[key] : nullptr;
        const json* a = after.contains(key) ? &after[key] : nullptr;
        if (b && a && *b == *a) continue;
        if (kMergedObjects.contains(key) && (!b || b->is_object()) && (!a || a->is_object())) {
            json sub = json::object();
            std::set<std::string> subKeys;
            for (const json* o : {b, a})
                if (o)
                    for (auto it = o->begin(); it != o->end(); ++it) subKeys.insert(it.key());
            for (const std::string& k : subKeys) {
                const bool inB = b && b->contains(k), inA = a && a->contains(k);
                if (inB && inA && (*b)[k] == (*a)[k]) continue;
                sub[k] = inA ? (*a)[k] : json(nullptr);
            }
            if (!sub.empty()) set[key] = sub;
        } else if (a) {
            set[key] = *a;
        } else {
            set[key] = json(nullptr);
        }
    }
    return set;
}

std::string describe(const json& before, const json& after) {
    std::string out;
    if (!after.is_object()) return out;
    for (auto it = after.begin(); it != after.end(); ++it) {
        const json was = before.is_object() && before.contains(it.key()) ? before[it.key()] : json(nullptr);
        joinInto(out, describeKey(it.key(), was, it.value()));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ rules

namespace {
bool hasAbility(const Character& c, const char* lowerName) {
    return std::ranges::any_of(c.abilities, [&](const Ref& r) {
        std::string n = r.name;
        std::ranges::transform(n, n.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return n == lowerName;
    });
}
}  // namespace

std::vector<Issue> rulesIssues(const Character& c) {
    std::vector<Issue> out;
    if (c.hp > maxHp(c)) out.push_back({"hp", "Hit points are above the maximum (" + std::to_string(c.hp) + " of " + std::to_string(maxHp(c)) + ")", std::to_string(c.hp) + "/" + std::to_string(maxHp(c))});
    if (c.wp > maxWp(c)) out.push_back({"wp", "Willpower points are above the maximum (" + std::to_string(c.wp) + " of " + std::to_string(maxWp(c)) + ")", std::to_string(c.wp) + "/" + std::to_string(maxWp(c))});
    if (c.hpBonus != 0 && !hasAbility(c, "robust")) out.push_back({"hpmax", "Maximum HP is changed by " + std::to_string(c.hpBonus) + " without the Robust ability", std::to_string(c.hpBonus)});
    if (c.wpBonus != 0 && !hasAbility(c, "focused")) out.push_back({"wpmax", "Maximum WP is changed by " + std::to_string(c.wpBonus) + " without the Focused ability", std::to_string(c.wpBonus)});
    for (int i = 0; i < kAttrCount; ++i)
        if (c.attr[i] < 3 || c.attr[i] > 18)
            out.push_back({std::string("attr:") + kAttrShort[i], std::string(kAttrLong[i]) + " " + std::to_string(c.attr[i]) + " is outside the 3 to 18 of the rules", std::to_string(c.attr[i])});
    for (const SkillEntry& s : c.skills)
        if (s.level > 18) out.push_back({"skill:" + s.ref.name, s.ref.name + " at " + std::to_string(s.level) + " is above the maximum of 18", std::to_string(s.level)});
    if (carriedItems(c) > encumbranceLimit(c))
        out.push_back({"encumbrance", "Carrying " + std::to_string(carriedItems(c)) + " items, the limit is " + std::to_string(encumbranceLimit(c)), std::to_string(carriedItems(c)) + "/" + std::to_string(encumbranceLimit(c))});
    return out;
}

std::vector<OpenIssue> openIssues(const Character& c) {
    std::vector<OpenIssue> out;
    for (const Issue& issue : rulesIssues(c)) {
        const auto ruling = c.reviews.find(issue.key);
        const bool judged = ruling != c.reviews.end() && ruling->second.value == issue.value;
        if (judged && ruling->second.status == "approved") continue;
        out.push_back({issue, judged ? "rejected" : "pending"});
    }
    return out;
}

void prune(Character& c) {
    const std::vector<Issue> issues = rulesIssues(c);
    std::erase_if(c.reviews, [&](const auto& entry) {
        return std::ranges::none_of(issues, [&](const Issue& i) { return i.key == entry.first && i.value == entry.second.value; });
    });
}

bool review(Character& c, const std::string& key, bool approve) {
    for (const Issue& issue : rulesIssues(c))
        if (issue.key == key) {
            c.reviews[key] = {approve ? "approved" : "rejected", issue.value, nowIso()};
            return true;
        }
    return false;
}

// ------------------------------------------------------------------------------------------- the file

EditResult editFile(const std::string& path, const std::string& characterId, const json& set, bool asPlayer, ChangeLog* log) {
    EditResult r;
    const auto raw = fs::readFile(path);
    json doc;
    if (!raw || !jsonParse(*raw, doc, nullptr) || !doc.is_object()) {
        r.status = 404;
        r.error = "That character does not exist any more.";
        return r;
    }
    if (asPlayer && jsonBool(doc, "locked")) {
        r.status = 423;
        r.error = "Your GM has locked your sheet.";
        return r;
    }
    const json before = doc;
    const Applied applied = applySet(doc, set, asPlayer);
    if (!applied.ok) {
        r.status = 400;
        r.error = applied.error;
        return r;
    }
    r.keys = applied.keys;
    Character after;
    Character::fromJson(doc.dump(), after, nullptr);
    after.id = characterId;
    if (!r.keys.empty()) {
        prune(after);                                                        // a ruling lasts only while what it judged is unchanged
        json reviews = json::object();
        for (const auto& [key, v] : after.reviews) reviews[key] = {{"status", v.status}, {"value", v.value}, {"at", v.at}};
        if (reviews.empty()) doc.erase("reviews");
        else doc["reviews"] = reviews;
        doc["revision"] = jsonInt(doc, "revision") + 1;
        doc["updated_at"] = nowIso();
        if (!fs::writeFile(path, jsonToYaml(doc))) {
            r.status = 500;
            r.error = "Could not save the change.";
            return r;
        }
        if (log) {
            json was = json::object(), now = json::object();
            for (const std::string& key : r.keys) {
                was[key] = before.contains(key) ? before[key] : json(nullptr);
                now[key] = doc[key];
            }
            ChangeEntry entry;
            entry.by = asPlayer ? "player" : "gm";
            entry.before = was;
            entry.after = now;
            entry.summary = describe(was, now);
            log->add(characterId, std::move(entry));
        }
    }
    Character::fromJson(doc.dump(), r.character, nullptr);
    r.character.id = characterId;
    r.ok = true;
    return r;
}

}  // namespace gm::sheet
