#include "web/web_names.h"

#include "game/encounter.h"
#include "parsing/fts.h"
#include "parsing/i18n.h"

namespace gm {
namespace {

constexpr std::initializer_list<Kind> kItems = {Kind::Weapon, Kind::Armor, Kind::Gear};

// "Torch x3" -> ("Torch", " x3"); a name without a count -> (name, "")
std::pair<std::string, std::string> splitCount(const std::string& text) {
    const size_t x = text.rfind(" x");
    if (x == std::string::npos || x + 2 >= text.size()) return {text, ""};
    for (size_t i = x + 2; i < text.size(); ++i)
        if (text[i] < '0' || text[i] > '9') return {text, ""};
    return {text.substr(0, x), text.substr(x)};
}

}  // namespace

const Entry* ShownNames::card(std::initializer_list<Kind> kinds, const std::string& key, const std::string& name) const {
    if (!key.empty())
        for (Kind k : kinds)
            if (const Entry* e = en.entry(k, en.idByKey(k, key))) return e;
    if (name.empty()) return nullptr;
    for (Kind k : kinds)
        if (const Entry* e = en.findByName(k, name)) return e;
    if (!same())                                         // the name as a page in another language shows it
        for (Kind k : kinds)
            if (const Entry* s = shown.findByName(k, name))
                if (const Entry* e = sameEntry(en, *s)) return e;
    return nullptr;
}

std::string ShownNames::show(std::initializer_list<Kind> kinds, const std::string& key, const std::string& name) const {
    if (same() || name.empty()) return name;
    const Entry* e = card(kinds, key, name);
    if (!e || lowerCopy(e->title) != lowerCopy(name)) return name;     // written by hand, or renamed: the sheet's own words
    const Entry* s = sameEntry(shown, *e);
    return s ? s->title : name;
}

std::string ShownNames::keep(std::initializer_list<Kind> kinds, const std::string& key, const std::string& name) const {
    if (same() || name.empty()) return name;
    for (Kind k : kinds) {                              // the title of a card as shown: its English title
        const Entry* s = nullptr;
        if (!key.empty()) s = shown.entry(k, shown.idByKey(k, key));
        if (!s || lowerCopy(s->title) != lowerCopy(name)) s = nullptr;
        if (!s)
            for (const Entry& x : shown.entries(k))
                if (lowerCopy(x.title) == lowerCopy(name)) {
                    s = &x;
                    break;
                }
        if (s)
            if (const Entry* e = sameEntry(en, *s)) return e->title;
    }
    return name;
}

std::string ShownNames::showAny(const std::string& name) const {
    if (same()) return name;
    for (int k = 0; k < kKindCount; ++k)
        if (const Entry* e = en.findByName(static_cast<Kind>(k), name))
            if (const Entry* s = sameEntry(shown, *e)) return s->title;
    for (const AttributeDef& a : en.attributes().attributes())                  // an attribute named in a message ("Strength")
        if (en.attributeName(a.id) == name) return shown.attributeName(a.id);
    return name;
}

std::string ShownNames::showTiny(const std::string& text) const {
    const auto [name, count] = splitCount(text);
    return show(kItems, "", name) + count;
}

std::string ShownNames::keepTiny(const std::string& text) const {
    const auto [name, count] = splitCount(text);
    return keep(kItems, "", name) + count;
}

std::string ShownNames::showCondition(const std::string& name) const { return i18n::tr(shown.language(), name); }

std::string ShownNames::keepCondition(const std::string& name) const {
    for (const ConditionInfo& c : kConditions)
        if (name == c.name || name == i18n::tr(shown.language(), c.name)) return c.name;
    return name;
}

namespace {

// Calls fn on the "name" of every object of a list (or of one object), with its "key".
template <class Fn>
void eachNamed(json& v, Fn fn) {
    const auto one = [&](json& o) {
        if (!o.is_object() || !o.contains("name") || !o["name"].is_string()) return;
        o["name"] = fn(jsonStr(o, "key"), o["name"].get<std::string>());
    };
    if (v.is_array())
        for (json& o : v) one(o);
    else one(v);
}

void mapDoc(json& doc, const ShownNames& n, bool toShown) {
    if (!doc.is_object()) return;
    const auto names = [&](std::initializer_list<Kind> kinds) {
        return [&n, kinds, toShown](const std::string& key, const std::string& name) { return toShown ? n.show(kinds, key, name) : n.keep(kinds, key, name); };
    };
    if (doc.contains("kin")) eachNamed(doc["kin"], names({Kind::Kin}));
    if (doc.contains("profession")) eachNamed(doc["profession"], names({Kind::Profession}));
    if (doc.contains("school") && doc["school"].is_string())
        doc["school"] = toShown ? n.show({Kind::Skill}, "", doc["school"].get<std::string>()) : n.keep({Kind::Skill}, "", doc["school"].get<std::string>());
    if (doc.contains("skills")) eachNamed(doc["skills"], names({Kind::Skill}));
    if (doc.contains("abilities")) eachNamed(doc["abilities"], names({Kind::Ability}));
    if (doc.contains("spells")) eachNamed(doc["spells"], names({Kind::Spell}));
    for (const char* key : {"weapons", "armor", "helmet", "inventory"})
        if (doc.contains(key)) eachNamed(doc[key], names(kItems));
    if (doc.contains("tiny_items") && doc["tiny_items"].is_array())
        for (json& t : doc["tiny_items"])
            if (t.is_string()) t = toShown ? n.showTiny(t.get<std::string>()) : n.keepTiny(t.get<std::string>());
    if (doc.contains("conditions") && doc["conditions"].is_array())
        for (json& c : doc["conditions"])
            if (c.is_string()) c = toShown ? n.showCondition(c.get<std::string>()) : n.keepCondition(c.get<std::string>());
}

}  // namespace

json ShownNames::showDoc(json doc) const {
    if (!same()) mapDoc(doc, *this, true);
    return doc;
}

json ShownNames::keepSet(json set) const {
    if (!same()) mapDoc(set, *this, false);
    return set;
}

}  // namespace gm
