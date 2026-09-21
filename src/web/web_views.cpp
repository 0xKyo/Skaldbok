#include "web/web_views.h"

#include <algorithm>
#include <cstdlib>

#include "encounter.h"
#include "sheet_edit.h"
#include "fts.h"

namespace gm {
namespace {

struct ContentType {
    const char* id;
    Kind kind;
    const char* label;
};
const ContentType kTypes[] = {
    {"spells", Kind::Spell, "Spells"},           {"abilities", Kind::Ability, "Abilities"}, {"skills", Kind::Skill, "Skills"},
    {"kin", Kind::Kin, "Kin"},                   {"professions", Kind::Profession, "Professions"}, {"weapons", Kind::Weapon, "Weapons"},
    {"armor", Kind::Armor, "Armor"},             {"gear", Kind::Gear, "Gear"},
};

const Entry* resolve(const ContentStore& cs, Kind kind, const Ref& r) {
    if (!r.key.empty())
        if (const Entry* e = cs.entry(kind, cs.idByKey(kind, r.key))) return e;
    return r.name.empty() ? nullptr : cs.findByName(kind, r.name);
}

// The raw parts of the sheet a player may change: what the editor works on and sends back as a set.
json editableDoc(const Character& c) {
    json all, mine = json::object();
    jsonParse(c.toJson(), all, nullptr);
    for (auto it = all.begin(); it != all.end(); ++it)
        if (sheet::playerMayEdit(it.key())) mine[it.key()] = it.value();
    return mine;
}

// What is against the rules on this sheet and has not been approved: the player sees it in red until it is fixed or the GM approves it.
json issuesView(const Character& c) {
    json out = json::array();
    for (const sheet::OpenIssue& i : sheet::openIssues(c)) out.push_back({{"key", i.key}, {"message", i.message}, {"status", i.status}});
    return out;
}

json fieldList(const std::vector<Field>& fields) {
    json out = json::array();
    for (const Field& f : fields) out.push_back({{"label", f.label}, {"value", f.value}});
    return out;
}

// An ability or spell: the full card if the content is loaded, otherwise just the name it was saved with.
json refView(const ContentStore& cs, Kind kind, const Ref& r) {
    if (const Entry* e = resolve(cs, kind, r)) {
        json card = publicCard(cs, *e);
        card["found"] = true;
        return card;
    }
    return {{"key", r.key}, {"kind", kindKey(kind)}, {"name", r.name.empty() ? r.key : r.name}, {"found", false}};
}

json itemView(const ContentStore& cs, const Item& it) {
    const Entry* card = nullptr;
    if (!it.key.empty())
        for (Kind k : {Kind::Weapon, Kind::Armor, Kind::Gear})
            if (!card) card = cs.entry(k, cs.idByKey(k, it.key));
    json stats = json::array();
    if (card)
        for (const Field& f : card->fields)
            if (f.label == "Damage" || f.label == "Grip" || f.label == "Range" || f.label == "Durability" || f.label == "Features" || f.label == "Armor rating")
                stats.push_back({{"label", f.label}, {"value", f.value}});
    return {{"name", it.name}, {"count", it.count}, {"note", it.note}, {"stats", stats}, {"description", card ? card->body : std::string()}};
}

json skillRows(const Character& c, const ContentStore& cs) {
    struct Row {
        std::string name, attribute, category;
        const SkillEntry* entry = nullptr;
        std::string key;                                 // the content key, so the editor can send it back
    };
    auto entryFor = [&](const std::string& key, const std::string& name) -> const SkillEntry* {
        for (const SkillEntry& s : c.skills)
            if ((!key.empty() && s.ref.key == key) || lowerCopy(s.ref.name) == lowerCopy(name)) return &s;
        return nullptr;
    };
    std::vector<Row> rows;
    std::vector<const SkillEntry*> used;
    for (const Entry& e : cs.entries(Kind::Skill)) {
        const std::string attribute = e.prop("attribute"), category = e.prop("category");
        if (attrIndex(attribute) < 0 || category == "magic") continue;              // schools come with the profession
        const SkillEntry* entry = entryFor(e.key, e.title);
        if (entry) used.push_back(entry);
        rows.push_back({e.title, attribute, category, entry, e.key});
    }
    for (const SkillEntry& s : c.skills)                                            // a school of magic, or a skill of a removed pack
        if (!std::ranges::contains(used, &s)) rows.push_back({s.ref.name, s.attribute, "other", &s, s.ref.key});
    auto rank = [](const std::string& cat) { return cat == "core" ? 0 : cat == "weapon" ? 1 : 2; };
    std::ranges::stable_sort(rows, [&](const Row& a, const Row& b) {
        return rank(a.category) != rank(b.category) ? rank(a.category) < rank(b.category) : lowerCopy(a.name) < lowerCopy(b.name);
    });
    json out = json::array();
    for (const Row& r : rows) {
        const int a = attrIndex(r.attribute);
        if (a < 0 && !r.entry) continue;
        out.push_back({{"name", r.name},
                       {"key", r.key},
                       {"attribute", r.attribute},
                       {"category", r.category},
                       {"level", skillLevel(c, r.entry, r.attribute)},
                       {"base", a >= 0 ? baseChance(c.attr[a]) : 0},
                       {"trained", r.entry && r.entry->trained},
                       {"marked", r.entry && r.entry->marked}});
    }
    return out;
}

}  // namespace

json publicCard(const ContentStore& cs, const Entry& e) {
    const SourceInfo* src = cs.source(e.sourceId);
    json page = nullptr;
    if (e.ref.valid()) page = e.ref.printed > 0 ? "p." + std::to_string(e.ref.printed) : "pdf p." + std::to_string(e.ref.page);
    else if (!e.pageNote.empty()) page = e.pageNote;
    return {{"key", e.key},
            {"kind", kindKey(e.kind)},
            {"name", e.title},
            {"subtitle", e.subtitle},
            {"fields", fieldList(e.fields)},
            {"body", e.body},
            {"source", src ? src->label : std::string()},
            {"homebrew", src && src->homebrew},
            {"page", page}};
}

json characterView(const Character& c, const ContentStore& cs, const Party* party) {
    const Entry* kin = resolve(cs, Kind::Kin, c.kin);
    const Entry* profession = resolve(cs, Kind::Profession, c.profession);
    const int kinMovement = kin ? std::atoi(kin->prop("movement").c_str()) : 0;
    const AgeRule& age = ageRule(c.age);

    json attributes = json::array();
    for (int i = 0; i < kAttrCount; ++i)
        attributes.push_back({{"key", kAttrShort[i]}, {"name", kAttrLong[i]}, {"value", c.attr[i]}, {"baseChance", baseChance(c.attr[i])}});
    json conditions = json::array();
    for (int i = 0; i < 6; ++i) conditions.push_back({{"name", kConditions[i].name}, {"attribute", kConditions[i].attribute}, {"active", (c.conditions & (1u << i)) != 0}});

    json abilities = json::array(), spells = json::array(), weapons = json::array(), inventory = json::array();
    for (const Ref& r : c.abilities) abilities.push_back(refView(cs, Kind::Ability, r));
    for (const Ref& r : c.spells) spells.push_back(refView(cs, Kind::Spell, r));
    for (const Item& it : c.weapons) weapons.push_back(itemView(cs, it));
    for (const Item& it : c.inventory) inventory.push_back(itemView(cs, it));

    return {{"name", c.name},
            {"nickname", c.nickname},
            {"player", c.player},
            {"age", {{"id", age.id}, {"label", age.label}}},
            {"kin", {{"name", c.kin.name}, {"description", kin ? kin->body : std::string()}}},
            {"profession", {{"name", c.profession.name}, {"description", profession ? profession->body : std::string()}}},
            {"school", c.school},
            {"party", party ? json(party->name) : json(nullptr)},
            {"attributes", attributes},
            {"hp", {{"current", c.hp}, {"max", maxHp(c)}, {"bonus", c.hpBonus}}},
            {"wp", {{"current", c.wp}, {"max", maxWp(c)}, {"bonus", c.wpBonus}}},
            {"conditions", conditions},
            {"derived",
             {{"movement", kinMovement > 0 ? json(kinMovement + movementModifier(c.attr[2])) : json(nullptr)},
              {"damageBonus", {{"str", damageBonus(c.attr[0])}, {"agl", damageBonus(c.attr[2])}}},
              {"encumbrance", {{"carried", carriedItems(c)}, {"limit", encumbranceLimit(c)}}},
              {"trainedSkills", age.trainedSkills}}},
            {"skills", skillRows(c, cs)},
            {"abilities", abilities},
            {"spells", spells},
            {"equipment",
             {{"weapons", weapons},
              {"armor", c.armor.name.empty() ? json(nullptr) : itemView(cs, c.armor)},
              {"helmet", c.helmet.name.empty() ? json(nullptr) : itemView(cs, c.helmet)},
              {"inventory", inventory},
              {"tinyItems", c.tinyItems},
              {"coins", {{"gold", c.gold}, {"silver", c.silver}, {"copper", c.copper}}}}},
            {"about", {{"weakness", c.weakness}, {"memento", c.memento}, {"appearance", c.appearance}, {"notes", c.notes}}},
            {"updatedAt", c.updatedAt},
            {"revision", c.revision},
            {"locked", c.locked},
            {"issues", issuesView(c)},
            {"doc", editableDoc(c)}};
}

json partyView(const Party& party, const std::function<const Character*(const std::string&)>& findCharacter, const std::string& meId) {
    json members = json::array();
    for (const std::string& id : party.members) {
        const Character* c = findCharacter(id);
        if (!c) continue;
        json conditions = json::array();
        for (int i = 0; i < 6; ++i)
            if (c->conditions & (1u << i)) conditions.push_back({{"name", kConditions[i].name}, {"attribute", kConditions[i].attribute}});
        members.push_back({{"name", c->name},
                           {"nickname", c->nickname},
                           {"player", c->player},
                           {"kin", c->kin.name},
                           {"profession", c->profession.name},
                           {"age", ageRule(c->age).label},
                           {"hp", {{"current", c->hp}, {"max", maxHp(*c)}}},
                           {"wp", {{"current", c->wp}, {"max", maxWp(*c)}}},
                           {"conditions", conditions},
                           {"you", c->id == meId}});
    }
    return {{"name", party.name}, {"members", members}};
}

json contentSummary(const ContentStore& cs) {
    json types = json::array(), packs = json::array();
    for (const ContentType& t : kTypes) types.push_back({{"id", t.id}, {"label", t.label}, {"count", cs.count(t.kind)}});
    for (const PackInfo& p : cs.packs())
        if (p.loaded) packs.push_back({{"id", p.id}, {"name", p.name}, {"version", p.version}, {"core", p.core}});
    return {{"types", types}, {"packs", packs}};
}

bool contentList(const ContentStore& cs, const std::string& typeId, const std::string& query, json& out) {
    const ContentType* type = nullptr;
    for (const ContentType& t : kTypes)
        if (typeId == t.id) type = &t;
    if (!type) return false;
    json entries = json::array();
    const std::string q = trimmed(query);
    if (q.empty()) {
        for (const Entry& e : cs.entries(type->kind)) entries.push_back(publicCard(cs, e));
    } else {
        for (const Hit& h : cs.search(q, 60, {type->kind}))
            if (const Entry* e = cs.entry(type->kind, h.id)) entries.push_back(publicCard(cs, *e));
    }
    out = {{"type", type->id}, {"label", type->label}, {"entries", entries}};
    return true;
}

}  // namespace gm
