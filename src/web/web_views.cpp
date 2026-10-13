#include "web/web_views.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <unordered_set>

#include "game/creation.h"
#include "game/encounter.h"
#include "game/sheet_edit.h"
#include "parsing/fts.h"
#include "parsing/i18n.h"
#include "web/web_names.h"

namespace gm {
namespace {

std::string tr(const ContentStore& shown, const std::string& english) { return i18n::tr(shown.language(), english); }

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
json editableDoc(const Character& c, bool asGm) {
    json all, mine = json::object();
    jsonParse(c.toJson(), all, nullptr);
    for (auto it = all.begin(); it != all.end(); ++it)
        if (sheet::playerMayEdit(it.key()) || (asGm && (it.key() == "kin" || it.key() == "profession" || it.key() == "school"))) mine[it.key()] = it.value();
    return mine;
}

// What is against the rules on this sheet and has not been approved: the player sees it in red until it is fixed or the GM approves it.
json issuesView(const Character& c, const ShownNames& names) {
    json out = json::array();
    const auto name = [&](const std::string& n) { return names.showAny(n); };
    const sheet::AttributeName english = [&](const std::string& id) { return names.en.attributeName(id); };        // the messages are English, then shown
    for (const sheet::OpenIssue& i : sheet::openIssues(c, english))
        out.push_back({{"key", i.key}, {"message", i18n::tr(names.shown.language(), i.message, name)}, {"status", i.status}});
    return out;
}

json fieldList(const std::vector<Field>& fields) {
    json out = json::array();
    for (const Field& f : fields) out.push_back({{"label", f.label}, {"value", f.value}});
    return out;
}

// An ability or spell: the full card if the content is loaded, otherwise just the name it was saved with.
json refView(const ContentStore& shown, Kind kind, const Ref& r) {
    if (const Entry* e = resolve(shown, kind, r)) {
        json card = publicCard(shown, *e);
        card["found"] = true;
        return card;
    }
    return {{"key", r.key}, {"kind", kindKey(kind)}, {"name", r.name.empty() ? r.key : r.name}, {"found", false}};
}

json itemView(const ShownNames& names, const Item& it) {
    const ContentStore& cs = names.en;
    const Entry* card = nullptr;
    if (!it.key.empty())
        for (Kind k : {Kind::Weapon, Kind::Armor, Kind::Gear})
            if (!card) card = cs.entry(k, cs.idByKey(k, it.key));
    const Entry* shownCard = card ? sameEntry(names.shown, *card) : nullptr;
    // what it weighs: its own weight, else the one in the book (by its key, or by its name when it was written by hand), else the usual
    double weight = itemWeight(it);
    if (it.weight < 0) {
        const Entry* book = card ? card : (it.key.empty() && !it.name.empty() ? cs.findByName(Kind::Gear, it.name) : nullptr);
        if (book && catalogWeight(*book) >= 0) weight = catalogWeight(*book);
    }
    // the numbers of a weapon or armor: "id" is the field's English label (what the page looks them up by), "label" as shown
    json stats = json::array();
    if (shownCard)
        for (const Field& f : shownCard->fields)
            for (const char* id : {"Damage", "Grip", "Range", "Durability", "Features", "Armor rating"})
                if (f.label == tr(names.shown, id)) stats.push_back({{"id", id}, {"label", f.label}, {"value", f.value}});
    // what wearing or wielding it does by itself: the skills that get a bane (armor, helmet) and whether a weapon is a ranged one
    const Entry* effect = card;                       // an item written by name only ("Chainmail") still has its effects
    for (Kind k : {Kind::Armor, Kind::Weapon})
        if (!effect && !it.name.empty()) effect = cs.findByName(k, it.name);
    const std::vector<std::string> banes = effect ? effect->list("banes") : std::vector<std::string>();
    json baneLabels = json::object();                 // the banes (English, like the skills' "id") as shown
    for (const std::string& b : banes) {
        const std::string skill = names.show({Kind::Skill}, "", b);
        baneLabels[b] = skill != b ? skill : tr(names.shown, b);
    }
    const bool ranged = effect && effect->kind == Kind::Weapon && effect->prop("kind") == "ranged";
    return {{"name", names.show({Kind::Weapon, Kind::Armor, Kind::Gear}, it.key, it.name)}, {"count", it.count}, {"note", it.note}, {"weight", weight},
            {"stats", stats}, {"banes", banes}, {"baneLabels", baneLabels}, {"ranged", ranged},
            {"description", shownCard ? shownCard->body : std::string()}};
}

json skillRows(const Character& c, const ShownNames& names) {
    const ContentStore& cs = names.en;
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
        if (cs.attributes().indexOf(attribute) < 0 || category == "magic") continue;              // schools come with the profession
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
        const std::string attributeId = cs.attributes().idOf(r.attribute);          // an old sheet may still hold the code ("STR")
        const bool known = !attributeId.empty();
        if (!known && !r.entry) continue;
        // "id": the English name (what the sheet's rule checks and an armor's banes name it by); "name" as shown
        out.push_back({{"id", r.name},
                       {"name", names.show({Kind::Skill}, r.key, r.name)},
                       {"key", r.key},
                       {"attribute", known ? attributeId : r.attribute},
                       {"attributeLabel", known ? names.shown.attributeAbbr(attributeId) : r.attribute},
                       {"category", r.category},
                       {"level", skillLevel(cs.attributes(), c, r.entry, r.attribute)},
                       {"base", known ? baseChance(cs.attributes(), c.attr.get(attributeId)) : 0},
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

// The cards that go with some text: every one a "{{include: Ref}}" line names, with what the page needs to draw it (nothing inside a card
// is included again). "ref" is what the line says.
json includesShown(const ContentStore& cs, const std::vector<const std::string*>& texts) {
    json out = json::array();
    std::set<std::string> seen;
    for (const std::string* text : texts) {
        size_t pos = 0;
        while (pos <= text->size()) {
            size_t end = text->find('\n', pos);
            if (end == std::string::npos) end = text->size();
            const std::string ref = includeMarkerRef(text->substr(pos, end - pos));
            pos = end + 1;
            if (ref.empty() || !seen.insert(ref).second) continue;
            if (const Entry* c = cs.resolveCard(ref))
                out.push_back({{"ref", ref}, {"key", c->key}, {"kind", kindKey(c->kind)}, {"name", c->title}, {"subtitle", c->subtitle}, {"fields", fieldList(c->fields)}, {"body", c->body}});
        }
    }
    return out;
}

// A content type of the index: what it is called, how many there are and the titles of the sections of its intro (a link may point at one).
json typeEntry(const ContentStore& cs, const ContentType& t) {
    json titles = json::array();                       // (and their English titles, which a link of a translated text may use)
    for (const auto& s : cs.introOf(t.kind).sections) titles.push_back(s.title);
    for (const auto& s : cs.introOf(t.kind).sections)
        if (!s.canon.empty() && s.canon != s.title) titles.push_back(s.canon);
    return {{"id", t.id}, {"label", tr(cs, t.label)}, {"count", cs.count(t.kind)}, {"sections", titles}};
}

// A kind's intro (null when it has none): body, sections and the tables they place.
json introView(const ContentStore& cs, const Intro& intro) {
    if (intro.empty()) return nullptr;
    json sects = json::array();
    for (const auto& s : intro.sections) sects.push_back({{"title", s.title}, {"canon", s.canon}, {"body", s.body}});
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

json ruleIncludes(const ContentStore& cs, const RuleNode& n) {
    std::vector<const std::string*> texts{&n.body};
    for (const auto& s : n.sections) texts.push_back(&s.body);
    return includesShown(cs, texts);
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
            {"includes", includesShown(cs, {&e.body})},
            {"kind", kindKey(e.kind)},
            {"name", e.title},
            {"subtitle", e.subtitle},
            {"slot", e.prop("slot")},                          // armor: "armor" or "helmet", where it is worn
            {"cost", e.prop("cost")},                          // weapons, armor, gear: the price in English ("10 silver"), to add up
            {"canon", e.canon},                                // its name in English
            {"fields", fieldList(e.fields)},
            {"body", e.body},
            {"source", src && src->homebrew ? src->label : std::string()},
            {"editedBy", e.editedBy},                          // the pack whose house rule replaced this card ("" if none)
            {"pack", packNameOf(cs, e.key)},
            {"homebrew", src && src->homebrew}};
}

json characterView(const Character& c, const ContentStore& cs, const ContentStore& shown, bool asGm) {
    const ShownNames names{cs, shown};
    const Entry* kin = resolve(cs, Kind::Kin, c.kin);
    const Entry* profession = resolve(cs, Kind::Profession, c.profession);
    const Entry* kinShown = kin ? sameEntry(shown, *kin) : nullptr;
    const Entry* professionShown = profession ? sameEntry(shown, *profession) : nullptr;
    const int kinMovement = kin ? std::atoi(kin->prop("movement").c_str()) : 0;
    const AgeDef& age = cs.age(c.age);
    Character weighed = c;                            // items never weighed by hand weigh what their card says
    fillWeights(weighed, cs);

    // "key" is the attribute's id (what the sheet keeps them by), "short" and "name" as shown
    const AttributeSet& rules = cs.attributes();
    json attributes = json::array();
    for (const AttributeDef& a : rules.attributes())
        attributes.push_back({{"key", a.id}, {"short", shown.text(a.abbrKey)}, {"name", shown.text(a.nameKey)}, {"description", shown.text(a.descriptionKey)},
                              {"value", c.attr.get(a.id)}, {"baseChance", baseChance(rules, c.attr.get(a.id))}});
    // the damage bonus of each attribute the table is for, as {key, short, value}
    json damageBonuses = json::array();
    if (const Lookup* bonus = rules.lookup("damage-bonus"))
        for (const std::string& id : bonus->attributes)
            damageBonuses.push_back({{"key", id}, {"short", shown.attributeAbbr(id)}, {"value", damageBonus(rules, c.attr.get(id))}});
    json conditions = json::array();
    for (int i = 0; i < 6; ++i)
        conditions.push_back({{"id", kConditions[i].name}, {"name", tr(shown, kConditions[i].name)}, {"attribute", kConditions[i].attribute},
                              {"attributeShort", shown.attributeAbbr(kConditions[i].attribute)}, {"active", (c.conditions & (1u << i)) != 0}});

    json abilities = json::array(), spells = json::array(), weapons = json::array(), inventory = json::array(), tiny = json::array();
    for (const Ref& r : c.abilities) abilities.push_back(refView(shown, Kind::Ability, r));
    for (const Ref& r : c.spells) spells.push_back(refView(shown, Kind::Spell, r));
    for (const Item& it : c.weapons) weapons.push_back(itemView(names, it));
    for (const Item& it : c.inventory) inventory.push_back(itemView(names, it));
    for (const std::string& t : c.tinyItems) tiny.push_back(names.showTiny(t));

    return {{"name", c.name},
            {"nickname", c.nickname},
            {"player", c.player},
            {"age", {{"id", age.id}, {"label", shown.age(c.age).name}}},
            {"kin", {{"name", names.show({Kind::Kin}, c.kin.key, c.kin.name)}, {"description", kinShown ? kinShown->prop("description") : std::string()}}},
            {"profession", {{"name", names.show({Kind::Profession}, c.profession.key, c.profession.name)}, {"description", professionShown ? professionShown->body : std::string()}}},
            {"school", names.show({Kind::Skill}, "", c.school)},
            {"attributes", attributes},
            {"hp", {{"current", c.hp}, {"max", maxHp(c)}, {"bonus", c.hpBonus}}},
            {"death", {{"successes", c.deathSuccesses}, {"failures", c.deathFailures}}},
            {"rest", {{"round", c.roundRest}, {"stretch", c.stretchRest}}},
            {"wp", {{"current", c.wp}, {"max", maxWp(c)}, {"bonus", c.wpBonus}}},
            {"conditions", conditions},
            {"derived",
             {{"movement", kinMovement > 0 ? json(kinMovement + movementModifier(rules, c.attr.get(kAgility))) : json(nullptr)},
              {"damageBonus", damageBonuses},
              {"encumbrance", {{"carried", carriedItems(weighed)}, {"limit", encumbranceLimit(c)}}},
              {"trainedSkills", age.trainedSkills()}}},
            {"skills", skillRows(c, names)},
            {"abilities", abilities},
            {"spells", spells},
            {"equipment",
             {{"weapons", weapons},
              {"armor", c.armor.name.empty() ? json(nullptr) : itemView(names, c.armor)},
              {"helmet", c.helmet.name.empty() ? json(nullptr) : itemView(names, c.helmet)},
              {"inventory", inventory},
              {"tinyItems", tiny},
              {"coins", {{"gold", c.gold}, {"silver", c.silver}, {"copper", c.copper}}}}},
            {"about", {{"weakness", c.weakness}, {"memento", c.memento}, {"appearance", c.appearance}, {"notes", c.notes}}},
            {"updatedAt", c.updatedAt},
            {"revision", c.revision},
            {"locked", c.locked},
            {"issues", issuesView(c, names)},
            {"doc", names.showDoc(editableDoc(c, asGm))}};
}

// --------------------------------------------------------------------------- Reference views (GM and players alike)

json contentSummary(const ContentStore& cs) {
    json types = json::array(), packs = json::array(), rules = json::array();
    for (const ContentType& t : kTypes) types.push_back(typeEntry(cs, t));
    int tinyCount = 0;                                  // the gear whose weight is "-": a category of its own, after Gear
    for (const Entry& e : cs.entries(Kind::Gear)) tinyCount += catalogWeight(e) == 0;
    types.push_back({{"id", "tiny"}, {"label", tr(cs, "Tiny items")}, {"count", tinyCount}, {"sections", json::array()}});
    types.push_back({{"id", "creatures"}, {"label", tr(cs, "Creatures")}, {"count", static_cast<int>(cs.monsters().size())}});
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
        rules.push_back({{"key", t.id}, {"title", tr(cs, title)}, {"introOnly", true}});
    }
    for (const RuleNode& n : cs.rules()) {
        if (n.parent != 0 || n.prop("nav").empty()) continue;
        const std::string key = n.key.substr(n.key.find_last_of('/') + 1);
        rules.push_back({{"key", key}, {"title", n.title}});
    }
    return {{"types", types}, {"packs", packs}, {"rules", rules}};
}

bool contentList(const ContentStore& cs, const std::string& typeId, const std::string& query, json& out) {
    // "tiny" is not a card type of its own: it is the gear that weighs nothing (the picker for the tiny items)
    const bool weightless = typeId == "tiny";
    static const ContentType kTiny{"tiny", Kind::Gear, "Tiny items"};
    const ContentType* type = weightless ? &kTiny : nullptr;
    for (const ContentType& t : kTypes)
        if (typeId == t.id) type = &t;
    if (!type) return false;
    json entries = json::array();
    const std::string q = trimmed(query);
    if (q.empty()) {
        for (const Entry& e : cs.entries(type->kind))
            if (!weightless || catalogWeight(e) == 0) entries.push_back(publicCard(cs, e));
    } else {
        for (const Hit& h : cs.search(q, 60, {type->kind}))
            if (const Entry* e = cs.entry(type->kind, h.id))
                if (!weightless || catalogWeight(*e) == 0) entries.push_back(publicCard(cs, *e));
    }
    out = {{"type", type->id}, {"label", tr(cs, type->label)}, {"entries", entries}, {"intro", introView(cs, cs.introOf(type->kind))}};
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
        json sects = json::array();
        for (const auto& s : n.sections) sects.push_back({{"title", s.title}, {"canon", s.canon}, {"body", s.body}});
        rules.push_back({{"key", n.key}, {"title", n.title}, {"canon", n.canon}, {"step", n.prop("step")}, {"tab", n.prop("tab")}, {"tool", n.prop("tool")},
                         {"body", n.body}, {"sections", sects}, {"tables", ruleTables(cs, n)}, {"includes", ruleIncludes(cs, n)}, {"parentId", n.parent}});
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

json monsterDetail(const ContentStore& cs, const Monster& m, const std::string& prefix) {
    const json image = m.image.empty() ? json(nullptr) : json(prefix + "/creatures/" + m.key + "/image");
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
        if (m.kind == "npc") sub = sub.empty() ? tr(cs, "NPC") : tr(cs, "NPC") + " · " + sub;
        else if (m.kind == "animal") sub = tr(cs, "Animal");
        const std::string tag = m.kind == "npc" || m.kind == "animal" ? m.kind : "monster";   // Monster: everything that is not an NPC or an animal
        list.push_back({{"id", m.id}, {"key", m.key}, {"name", m.name}, {"sub", sub}, {"kind", tag}});
    }
    // Sort by name
    std::sort(list.begin(), list.end(), [](const json& a, const json& b) {
        return lowerAscii(a["name"].get<std::string>()) < lowerAscii(b["name"].get<std::string>());
    });
    return {{"creatures", list}, {"intro", introView(cs, cs.introOf(Kind::Monster))}};
}

json characterSummary(const Character& c, const ContentStore& content, const ContentStore& shown, const std::string& link) {
    const ShownNames names{content, shown};
    json conditions = json::array();
    for (int i = 0; i < 6; ++i)
        if (c.conditions & (1u << i)) conditions.push_back(tr(shown, kConditions[i].name));
    const AgeDef& age = shown.age(c.age);
    return {{"id", c.id},
            {"name", c.name.empty() ? c.id : c.name},
            {"player", c.player},
            {"kin", names.show({Kind::Kin}, c.kin.key, c.kin.name)},
            {"profession", names.show({Kind::Profession}, c.profession.key, c.profession.name)},
            {"age", shown.age(c.age).name},
            {"hp", {{"current", c.hp}, {"max", maxHp(c)}}},
            {"wp", {{"current", c.wp}, {"max", maxWp(c)}}},
            {"conditions", conditions},
            {"link", link}};
}

}  // namespace gm
