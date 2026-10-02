#include "web/web_creation.h"

#include <algorithm>
#include <cstdlib>

namespace gm {
namespace {

std::string firstSentence(const std::string& s, size_t max = 160) {
    std::string t = s.substr(0, s.find('\n'));
    if (t.size() > max) {
        size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;      // never in the middle of a character
        t = t.substr(0, cut) + "…";
    }
    return t;
}

json stringsJson(const std::vector<std::string>& v) {
    json a = json::array();
    for (const std::string& s : v) a.push_back(s);
    return a;
}

json fieldsJson(const std::vector<Field>& fields) {
    json a = json::array();
    for (const Field& f : fields) a.push_back({{"label", f.label}, {"value", f.value}});
    return a;
}

// What each face of the table's die gives (index 0 = face 1), so the browser rolls without knowing how rows cover the faces.
json facesOf(const DataTable& t) {
    json faces = json::array();
    if (t.rows.empty()) return faces;
    const int sides = t.dieSides() > 0 ? t.dieSides() : static_cast<int>(t.rows.size());
    for (int roll = 1; roll <= sides; ++roll) {
        int row = t.rowForRoll(roll);
        if (row < 0) row = std::min(roll, static_cast<int>(t.rows.size())) - 1;
        const auto& cells = t.rows[static_cast<size_t>(row)].cells;
        faces.push_back(cells.empty() ? std::string() : cells.front());
    }
    return faces;
}

// A table of the Rulebook itself ("Kin", "Profession"): the book rolls kin on a D12 and profession on a D10.
json bookFaces(const ContentStore& content, const char* title) {
    const int rulebook = content.bookId("rulebook");
    for (const DataTable& t : content.packTables())
        if (t.title == title && t.sourceId == rulebook) return facesOf(t);
    return json::array();
}

json rolesJson(const ContentStore& content, const char* role) {
    json out = json::array();
    for (const DataTable* t : content.tablesByRole(role)) out.push_back({{"title", t->title}, {"dice", t->dice}, {"faces", facesOf(*t)}});
    return out;
}

json abilityBrief(const ContentStore& content, const std::string& name) {
    const Entry* a = content.findByName(Kind::Ability, name);
    return {{"name", name},
            {"wpCost", a ? a->prop("wp_cost") : std::string()},
            {"summary", a ? firstSentence(a->body) : std::string()},
            {"fields", a ? fieldsJson(a->fields) : json::array()},
            {"body", a ? a->body : std::string()}};
}

json kinJson(const ContentStore& content, const Entry& e) {
    json innate = json::array();
    for (const std::string& n : e.list("innate_abilities")) innate.push_back(abilityBrief(content, n));
    const SourceInfo* si = content.source(e.sourceId);
    return {{"key", e.key},           {"title", e.title},   {"movement", e.prop("movement")}, {"innate", innate},
            {"names", stringsJson(e.list("names"))}, {"body", e.body}, {"source", si && si->homebrew ? si->label : std::string()}};
}

json professionJson(const ContentStore& content, const Entry& e) {
    json p = {{"key", e.key},
              {"title", e.title},
              {"keyAttribute", e.prop("key_attribute")},
              {"schools", stringsJson(e.list("schools"))},
              {"skills", stringsJson(e.list("skills"))},
              {"nicknames", stringsJson(e.list("nicknames"))},
              {"body", e.body},
              {"fields", fieldsJson(e.fields)}};
    json bySchool = json::object();
    for (const std::string& s : e.list("schools")) bySchool[s] = stringsJson(e.list("skills." + s));
    p["skillsBySchool"] = bySchool;
    json heroic = json::array();
    for (const std::string& h : e.list("heroic_abilities")) heroic.push_back(abilityBrief(content, h));
    p["heroic"] = heroic;
    p["magic"] = {{"spells", std::atoi(e.prop("magic.spells").c_str())}, {"tricks", std::atoi(e.prop("magic.tricks").c_str())}, {"rank", std::atoi(e.prop("magic.spell_rank").c_str())}};
    json sets = json::array();
    for (const std::string& text : e.list("starting_gear")) {
        json picks = json::array();
        for (const GearPick& g : parseGearSet(text)) picks.push_back({{"options", stringsJson(g.options)}, {"diceSides", g.diceSides}, {"what", g.what}});
        sets.push_back({{"text", text}, {"picks", picks}});
    }
    p["gearSets"] = sets;
    const SourceInfo* si = content.source(e.sourceId);
    p["source"] = si && si->homebrew ? si->label : std::string();
    return p;
}

int intOf(const json& o, const char* key, int def) { return jsonInt(o, key, def); }

}  // namespace

json creationCatalog(const ContentStore& content) {
    json c = json::object();
    json kins = json::array(), profs = json::array();
    for (const Entry& e : content.entries(Kind::Kin)) kins.push_back(kinJson(content, e));
    for (const Entry& e : content.entries(Kind::Profession)) profs.push_back(professionJson(content, e));
    c["kins"] = kins;
    c["professions"] = profs;

    json ages = json::array();
    for (const std::string id : {"young", "adult", "old"}) {
        const AgeRule& r = ageRule(id);
        json mods = json::array();
        for (int m : r.attrMod) mods.push_back(m);
        ages.push_back({{"id", r.id}, {"label", r.label}, {"summary", r.summary}, {"trainedSkills", r.trainedSkills}, {"attrMod", mods}});
    }
    c["ages"] = ages;

    json attrs = json::array();
    for (int i = 0; i < kAttrCount; ++i) attrs.push_back({{"short", kAttrShort[i]}, {"long", kAttrLong[i]}});
    c["attributes"] = attrs;
    json chance = json::array();                               // base chance of a score, index = score
    for (int v = 0; v <= 18; ++v) chance.push_back(baseChance(v));
    c["baseChance"] = chance;
    c["professionSkills"] = kProfessionSkills;

    json skillAttr = json::object(), selectable = json::array();
    for (const Entry& e : content.entries(Kind::Skill)) skillAttr[e.title] = e.prop("attribute");
    for (const Entry* e : selectableSkills(content)) selectable.push_back({{"title", e->title}, {"attribute", e->prop("attribute")}, {"summary", firstSentence(e->body)}});
    c["skillAttributes"] = skillAttr;
    c["selectableSkills"] = selectable;

    json heroicAll = json::array();
    for (const Entry& a : content.entries(Kind::Ability))
        if (a.prop("type") == "heroic") heroicAll.push_back(abilityBrief(content, a.title));
    c["heroicAbilities"] = heroicAll;

    json spells = json::array();
    for (const Entry& s : content.entries(Kind::Spell))
        spells.push_back({{"key", s.key}, {"title", s.title}, {"school", s.prop("school")}, {"trick", s.prop("trick") == "1"}, {"rank", std::atoi(s.prop("rank").c_str())}, {"summary", firstSentence(s.body)}});
    c["spells"] = spells;

    c["tables"] = {{"kin", bookFaces(content, "Kin")},
                   {"profession", bookFaces(content, "Profession")},
                   {"weakness", rolesJson(content, "weakness")},
                   {"memento", rolesJson(content, "memento")},
                   {"appearance", rolesJson(content, "appearance")}};
    return c;
}

Creation creationFromJson(const json& body, const ContentStore& content) {
    Creation c;
    auto known = [&](Kind k, const std::string& key) { return content.idByKey(k, key) != 0 ? key : std::string(); };
    c.kinKey = known(Kind::Kin, jsonStr(body, "kin"));
    c.professionKey = known(Kind::Profession, jsonStr(body, "profession"));
    c.age = ageRule(jsonStr(body, "age", "adult")).id;
    c.school = jsonStr(body, "school");
    if (const json* r = jsonFind(body, "rolled"); r && r->is_array())
        for (size_t i = 0; i < r->size() && i < static_cast<size_t>(kAttrCount); ++i)
            if ((*r)[i].is_number_integer()) c.rolled[i] = (*r)[i].get<int>();
    c.professionSkills = jsonStrings(body, "professionSkills");
    c.freeSkills = jsonStrings(body, "freeSkills");
    c.heroicAbility = jsonStr(body, "heroicAbility");
    c.spells = jsonStrings(body, "spells");
    c.name = jsonStr(body, "name");
    c.nickname = jsonStr(body, "nickname");
    c.player = jsonStr(body, "player");
    c.weakness = jsonStr(body, "weakness");
    c.memento = jsonStr(body, "memento");
    c.appearance = jsonStr(body, "appearance");

    if (const json* custom = jsonFind(body, "customGear"); custom && custom->is_array())     // free choice instead of a set
        for (const json& item : *custom) {
            const std::string key = jsonStr(item, "key");
            const int count = std::clamp(intOf(item, "count", 1), 1, 99);
            if (!key.empty() && custom->size() <= 60) c.customGear.emplace_back(key, count);
        }

    // the gear set is read again from the profession, so the browser can only choose among the printed options
    const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, c.professionKey));
    const int set = intOf(body, "gearSet", -1);
    if (prof && set >= 0 && set < static_cast<int>(prof->list("starting_gear").size())) {
        c.gearSet = set;
        c.gear = parseGearSet(prof->list("starting_gear")[static_cast<size_t>(set)]);
        const json* picks = jsonFind(body, "gear");
        for (size_t i = 0; i < c.gear.size(); ++i) {
            if (!picks || !picks->is_array() || i >= picks->size()) continue;
            GearPick& g = c.gear[i];
            const json& p = (*picks)[i];
            g.choice = std::clamp(intOf(p, "choice", 0), 0, static_cast<int>(g.options.size()) - 1);
            g.rolled = g.diceSides > 0 ? std::clamp(intOf(p, "rolled", 0), 0, g.diceSides) : 0;
        }
    }
    return c;
}

json creationToJson(const Creation& c) {
    json rolled = json::array();
    for (int v : c.rolled) rolled.push_back(v);
    json gear = json::array();
    for (const GearPick& g : c.gear) gear.push_back({{"choice", g.choice}, {"rolled", g.rolled}});
    json custom = json::array();
    for (const auto& [key, count] : c.customGear) custom.push_back({{"key", key}, {"count", count}});
    return {{"customGear", custom},
            {"kin", c.kinKey},
            {"profession", c.professionKey},
            {"age", c.age},
            {"school", c.school},
            {"rolled", rolled},
            {"professionSkills", stringsJson(c.professionSkills)},
            {"freeSkills", stringsJson(c.freeSkills)},
            {"heroicAbility", c.heroicAbility},
            {"spells", stringsJson(c.spells)},
            {"gearSet", c.gearSet},
            {"gear", gear},
            {"name", c.name},
            {"nickname", c.nickname},
            {"player", c.player},
            {"weakness", c.weakness},
            {"memento", c.memento},
            {"appearance", c.appearance}};
}

json creationPreview(const Creation& c, const ContentStore& content, Dice& dice) {
    const std::vector<std::string> problems = validateCreation(c, content);
    json out = {{"problems", stringsJson(problems)}};

    int shown[kAttrCount], final[kAttrCount];
    for (int i = 0; i < kAttrCount; ++i) shown[i] = std::max(3, c.rolled[i]);
    applyAge(shown, c.age, final);
    Character sheet;
    std::copy(final, final + kAttrCount, sheet.attr);
    const Entry* kin = content.entry(Kind::Kin, content.idByKey(Kind::Kin, c.kinKey));
    const int move = kin ? std::atoi(kin->prop("movement").c_str()) : 0;
    json attrs = json::array();
    for (int v : final) attrs.push_back(v);
    out["attributes"] = attrs;
    out["derived"] = {{"hp", maxHp(sheet)},
                      {"wp", maxWp(sheet)},
                      {"movement", move ? json(move + movementModifier(final[2])) : json(nullptr)},
                      {"damageStr", damageBonus(final[0])},
                      {"damageAgl", damageBonus(final[2])},
                      {"encumbrance", encumbranceLimit(sheet)}};
    out["summary"] = problems.empty() ? buildCharacter(c, content, dice).summary() : std::string();
    return out;
}

}  // namespace gm
