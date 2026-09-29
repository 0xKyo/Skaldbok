#include "web/web_views.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "game/encounter.h"
#include "game/sheet_edit.h"
#include "parsing/fts.h"

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
            if (f.label == "Damage" || f.label == "Grip" || f.label == "Range" || f.label == "Durability" || f.label == "Features" || f.label == "Armor rating") {
                stats.push_back({{"label", f.label}, {"value", f.value}});
            }
    // what wearing or wielding it does by itself: the skills that get a bane (armor, helmet) and whether a weapon is a ranged one
    const Entry* effect = card;                       // an item written by name only ("Chainmail") still has its effects
    for (Kind k : {Kind::Armor, Kind::Weapon})
        if (!effect && !it.name.empty()) effect = cs.findByName(k, it.name);
    const std::vector<std::string> banes = effect ? effect->list("banes") : std::vector<std::string>();
    const bool ranged = effect && effect->kind == Kind::Weapon && effect->prop("kind") == "ranged";
    return {{"name", it.name}, {"count", it.count}, {"note", it.note}, {"stats", stats}, {"banes", banes}, {"ranged", ranged},
            {"description", card ? card->body : std::string()}};
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

json tableJson(const DataTable& t) {
    json rows = json::array();
    for (const TableRow& r : t.rows) rows.push_back({{"roll", r.rollText}, {"cells", r.cells}});
    return {{"title", t.title}, {"dice", t.dice}, {"columns", t.columns}, {"rows", rows}};
}

// The name in a "{{table: Name}}" line, empty if the line is not one.
std::string tableMarkerName(const std::string& line) {
    const auto b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = line.find_last_not_of(" \t\r");
    const std::string t = line.substr(b, e - b + 1);
    if (!t.starts_with("{{table:") || !t.ends_with("}}")) return {};
    return trimmed(t.substr(8, t.size() - 10));
}

// The tables that go with some text: the ones it owns, plus every one a "{{table: Name}}" line names (any table, by its title).
json tablesShown(const ContentStore& cs, const std::vector<const DataTable*>& own, const std::vector<const std::string*>& texts) {
    std::vector<const DataTable*> all = own;
    for (const std::string* text : texts) {
        size_t pos = 0;
        while (pos <= text->size()) {
            size_t end = text->find('\n', pos);
            if (end == std::string::npos) end = text->size();
            const std::string name = tableMarkerName(text->substr(pos, end - pos));
            if (!name.empty())
                if (const DataTable* t = cs.tableByName(name)) all.push_back(t);
            pos = end + 1;
        }
    }
    json out = json::array();
    std::unordered_set<const DataTable*> seen;
    for (const DataTable* t : all)
        if (t && seen.insert(t).second) out.push_back(tableJson(*t));
    return out;
}

// A content type of the index: what it is called, how many there are and the titles of the sections of its intro (a link may point at one).
json typeEntry(const ContentStore& cs, const ContentType& t) {
    json titles = json::array();
    for (const auto& s : cs.introOf(t.kind).sections) titles.push_back(s.title);
    return {{"id", t.id}, {"label", t.label}, {"count", cs.count(t.kind)}, {"sections", titles}};
}

// A kind's intro (null when it has none): body, sections and the tables they place.
json introView(const ContentStore& cs, const Intro& intro) {
    if (intro.empty()) return nullptr;
    json sects = json::array();
    for (const auto& s : intro.sections) sects.push_back({{"title", s.title}, {"body", s.body}});
    std::vector<const DataTable*> own;
    std::vector<const std::string*> texts{&intro.body};
    for (const DataTable& t : intro.tables) own.push_back(&t);
    for (const auto& s : intro.sections) texts.push_back(&s.body);
    return {{"body", intro.body}, {"sections", sects}, {"tables", tablesShown(cs, own, texts)}};
}

json ruleTables(const ContentStore& cs, const RuleNode& n);

// The text of a chapter's own root rule is that page's intro (null when the root has none).
json chapterIntro(const ContentStore& cs, const RuleNode& root) {
    if (root.body.empty() && root.sections.empty()) return nullptr;
    json sects = json::array();
    for (const auto& s : root.sections) sects.push_back({{"title", s.title}, {"body", s.body}});
    return {{"body", root.body}, {"sections", sects}, {"tables", ruleTables(cs, root)}};
}

json ruleTables(const ContentStore& cs, const RuleNode& n) {
    std::vector<const DataTable*> own;
    for (int id : cs.tablesOfRule(n.id)) own.push_back(cs.packTable(id));
    std::vector<const std::string*> texts{&n.body};
    for (const auto& s : n.sections) texts.push_back(&s.body);
    return tablesShown(cs, own, texts);
}

}  // namespace

// The name of the pack an entry or creature comes from: its key is "<pack>/<kind>/<id>" ("Dragonbane Core", "Frostmarch Tales").
static std::string packNameOf(const ContentStore& cs, const std::string& key) {
    const PackInfo* pk = cs.pack(key.substr(0, key.find('/')));
    return pk ? pk->name : std::string();
}

// Builds the JSON card shown to players. "pack" is where it comes from (the page shows it as a Source tag); "source" is the label of
// a homebrew source only.
json publicCard(const ContentStore& cs, const Entry& e) {
    const SourceInfo* src = cs.source(e.sourceId);
    std::vector<const DataTable*> own;
    for (const DataTable& t : e.tables) own.push_back(&t);
    return {{"key", e.key},
            {"tables", tablesShown(cs, own, {&e.body})},
            {"kind", kindKey(e.kind)},
            {"name", e.title},
            {"subtitle", e.subtitle},
            {"fields", fieldList(e.fields)},
            {"body", e.body},
            {"source", src && src->homebrew ? src->label : std::string()},
            {"pack", packNameOf(cs, e.key)},
            {"homebrew", src && src->homebrew}};
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
    json types = json::array(), packs = json::array(), rules = json::array();
    for (const ContentType& t : kTypes) types.push_back(typeEntry(cs, t));
    for (const PackInfo& p : cs.packs()) {
        int shown = 0;                                   // a pack that only brings rules or tables has nothing to show here
        for (const ContentType& t : kTypes) shown += p.counts[static_cast<int>(t.kind)];
        if (p.loaded && shown > 0) packs.push_back({{"id", p.id}, {"name", p.name}, {"version", p.version}, {"core", p.core}});
    }
    // Intro-only chapters: spells (Magic) and skills (Skills) surfaced in the Rules tab
    for (const ContentType& t : kTypes) {
        if (t.kind != Kind::Spell && t.kind != Kind::Skill && t.kind != Kind::Gear) continue;
        const Intro& intro = cs.introOf(t.kind);
        if (intro.empty()) continue;
        const char* title = t.kind == Kind::Spell ? "Magic" : t.kind == Kind::Skill ? "Skills" : "Gear";
        rules.push_back({{"key", t.id}, {"title", title}, {"introOnly", true}});
    }
    for (const RuleNode& n : cs.rules()) {
        if (n.parent != 0 || n.prop("nav").empty() || !n.prop("web_hide").empty()) continue;
        const std::string key = n.key.substr(n.key.find_last_of('/') + 1);
        rules.push_back({{"key", key}, {"title", n.title}});
    }
    return {{"types", types}, {"packs", packs}, {"rules", rules}};
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
    out = {{"type", type->id}, {"label", type->label}, {"entries", entries}, {"intro", introView(cs, cs.introOf(type->kind))}};
    return true;
}

bool rulesChapter(const ContentStore& cs, const std::string& keyId, json& out) {
    const RuleNode* chapter = nullptr;
    for (const RuleNode& n : cs.rules()) {
        if (n.parent != 0 || n.prop("nav").empty()) continue;
        const std::string k = n.key.substr(n.key.find_last_of('/') + 1);
        if (k == keyId) { chapter = &n; break; }
    }
    if (!chapter) return false;
    std::unordered_set<int> inChapter;
    inChapter.insert(chapter->id);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const RuleNode& n : cs.rules())
            if (!inChapter.count(n.id) && inChapter.count(n.parent))
                { inChapter.insert(n.id); changed = true; }
    }
    json rules = json::array();
    for (const RuleNode& n : cs.rules()) {
        if (!inChapter.count(n.id)) continue;
        if (n.id == chapter->id) continue;                 // its own text goes out as the page's intro
        if (!n.prop("web_hide").empty()) continue;
        json sects = json::array();
        for (const auto& s : n.sections) sects.push_back({{"title", s.title}, {"body", s.body}});
        rules.push_back({{"key", n.key}, {"title", n.title}, {"step", n.prop("step")}, {"tab", n.prop("tab")},
                         {"body", n.body}, {"sections", sects}, {"tables", ruleTables(cs, n)}, {"parentId", n.parent}});
    }
    const json intro = chapter->prop("web_hide").empty() ? chapterIntro(cs, *chapter) : json(nullptr);   // a hidden chapter keeps its text hidden
    out = {{"key", keyId}, {"title", chapter->title}, {"layout", chapter->prop("layout")}, {"intro", intro}, {"rules", rules}};
    return true;
}

// --------------------------------------------------------------------------- GM-only views

json contentSummaryGm(const ContentStore& cs) {
    json types = json::array(), packs = json::array(), rules = json::array();
    for (const ContentType& t : kTypes) types.push_back(typeEntry(cs, t));
    types.push_back({{"id", "creatures"}, {"label", "Creatures"}, {"count", static_cast<int>(cs.monsters().size())}});
    for (const PackInfo& p : cs.packs()) {
        int shown = 0;
        for (const ContentType& t : kTypes) shown += p.counts[static_cast<int>(t.kind)];
        if (p.loaded && shown > 0) packs.push_back({{"id", p.id}, {"name", p.name}, {"version", p.version}, {"core", p.core}});
    }
    for (const ContentType& t : kTypes) {
        if (t.kind != Kind::Spell && t.kind != Kind::Skill && t.kind != Kind::Gear) continue;
        const Intro& intro = cs.introOf(t.kind);
        if (intro.empty()) continue;
        const char* title = t.kind == Kind::Spell ? "Magic" : t.kind == Kind::Skill ? "Skills" : "Gear";
        rules.push_back({{"key", t.id}, {"title", title}, {"introOnly", true}});
    }
    for (const RuleNode& n : cs.rules()) {
        if (n.parent != 0 || n.prop("nav").empty()) continue;
        const std::string key = n.key.substr(n.key.find_last_of('/') + 1);
        rules.push_back({{"key", key}, {"title", n.title}});
    }
    return {{"types", types}, {"packs", packs}, {"rules", rules}};
}

bool rulesChapterGm(const ContentStore& cs, const std::string& keyId, json& out) {
    const RuleNode* chapter = nullptr;
    for (const RuleNode& n : cs.rules()) {
        if (n.parent != 0 || n.prop("nav").empty()) continue;
        const std::string k = n.key.substr(n.key.find_last_of('/') + 1);
        if (k == keyId) { chapter = &n; break; }
    }
    if (!chapter) return false;
    std::unordered_set<int> inChapter;
    inChapter.insert(chapter->id);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const RuleNode& n : cs.rules())
            if (!inChapter.count(n.id) && inChapter.count(n.parent))
                { inChapter.insert(n.id); changed = true; }
    }
    json rules = json::array();
    for (const RuleNode& n : cs.rules()) {
        if (!inChapter.count(n.id)) continue;
        if (n.id == chapter->id) continue;                 // its own text goes out as the page's intro
        json sects = json::array();
        for (const auto& s : n.sections) sects.push_back({{"title", s.title}, {"body", s.body}});
        rules.push_back({{"key", n.key}, {"title", n.title}, {"step", n.prop("step")}, {"tab", n.prop("tab")}, {"tool", n.prop("tool")},
                         {"body", n.body}, {"sections", sects}, {"tables", ruleTables(cs, n)}, {"parentId", n.parent}});
    }
    out = {{"key", keyId}, {"title", chapter->title}, {"layout", chapter->prop("layout")}, {"intro", chapterIntro(cs, *chapter)}, {"rules", rules}};
    return true;
}

json npcLists(const ContentStore& cs) {
    json lists = json::array();
    for (const NpcList& l : cs.npcLists()) lists.push_back({{"key", l.key}, {"dice", l.dice}, {"values", l.values}});
    return {{"lists", lists}};
}

static std::string lowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

json monsterDetail(const ContentStore& cs, const Monster& m) {
    const json image = m.image.empty() ? json(nullptr) : json("/gm/creatures/" + m.key + "/image");
    json blocks = json::array();
    for (const StatBlock& b : m.blocks) {
        json fields = json::array();
        for (const Field& f : b.fields) fields.push_back({{"label", f.label}, {"value", f.value}});
        blocks.push_back({{"variant", b.variant}, {"fields", fields}});
    }
    json attacks = json::array();
    for (const Attack& a : m.attacks)
        attacks.push_back({{"rollMin", a.rollMin}, {"rollMax", a.rollMax}, {"rollText", a.rollText},
                           {"name", a.name}, {"text", a.text}});
    json abilities = json::array();
    for (const NamedText& a : m.abilities)
        abilities.push_back({{"name", a.name}, {"kind", a.kind}, {"text", a.text}});
    json tables = json::array();
    for (const TableRef& t : m.tables) tables.push_back({{"id", t.id}, {"title", t.title}});
    return {{"id", m.id}, {"key", m.key}, {"name", m.name}, {"kind", m.kind},
            {"category", m.category}, {"description", m.description}, {"quote", m.quote},
            {"randomEncounter", m.randomEncounter}, {"adventureSeed", m.adventureSeed},
            {"statsRef", m.statsRef}, {"pack", packNameOf(cs, m.key)}, {"image", image}, {"blocks", blocks}, {"attacks", attacks},
            {"abilities", abilities}, {"tables", tables}};
}

json monsterList(const ContentStore& cs, const std::string& query) {
    const std::string q = lowerAscii(trimmed(query));
    json list = json::array();
    for (const Monster& m : cs.monsters()) {
        if (!q.empty() && lowerAscii(m.name).find(q) == std::string::npos) continue;
        std::string sub = m.category;
        if (m.kind == "npc") sub = sub.empty() ? "NPC" : "NPC · " + sub;
        else if (m.kind == "animal") sub = "Animal";
        const std::string tag = m.kind == "npc" || m.kind == "animal" ? m.kind : "monster";   // Monster: everything that is not an NPC or an animal
        list.push_back({{"id", m.id}, {"key", m.key}, {"name", m.name}, {"sub", sub}, {"kind", tag}});
    }
    // Sort by name
    std::sort(list.begin(), list.end(), [](const json& a, const json& b) {
        return lowerAscii(a["name"].get<std::string>()) < lowerAscii(b["name"].get<std::string>());
    });
    return {{"creatures", list}, {"intro", introView(cs, cs.introOf(Kind::Monster))}};
}

json characterSummary(const Character& c, const ContentStore& /*content*/, const std::string& link) {
    json conditions = json::array();
    for (int i = 0; i < 6; ++i)
        if (c.conditions & (1u << i)) conditions.push_back(kConditions[i].name);
    const AgeRule& age = ageRule(c.age);
    return {{"id", c.id},
            {"name", c.name.empty() ? c.id : c.name},
            {"player", c.player},
            {"kin", c.kin.name},
            {"profession", c.profession.name},
            {"age", age.label},
            {"hp", {{"current", c.hp}, {"max", maxHp(c)}}},
            {"wp", {{"current", c.wp}, {"max", maxWp(c)}}},
            {"conditions", conditions},
            {"link", link}};
}

}  // namespace gm
