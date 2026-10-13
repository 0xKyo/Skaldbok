#include "web/web_creation.h"

#include <algorithm>
#include <cstdlib>

#include "parsing/i18n.h"
#include "web/web_names.h"

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

// A table of the Rulebook itself ("Kin", "Profession"): the book rolls kin on a D12 and profession on a D10. In English: a face is
// matched against the cards' English titles ("canon").
json bookFaces(const ContentStore& content, const char* title) {
    const int rulebook = content.bookId("rulebook");
    for (const DataTable& t : content.packTables())
        if (t.title == title && t.sourceId == rulebook) return facesOf(t);
    return json::array();
}

// The same table in the content shown (by its key), or the English one.
const DataTable* shownTable(const ContentStore& shown, const DataTable& t) {
    if (const DataTable* s = shown.packTable(shown.idByKey(Kind::Table, t.key))) return s;
    for (const DataTable* s : shown.tablesByRole(t.role))
        if (s->key == t.key) return s;
    return &t;
}

// The tables the creator rolls a text on (weakness, memento, appearance), as shown: what is rolled is the character's own text.
json rolesJson(const ContentStore& content, const ContentStore& shown, const char* role) {
    json out = json::array();
    for (const DataTable* t : content.tablesByRole(role)) {
        const DataTable* s = shownTable(shown, *t);
        out.push_back({{"title", s->title}, {"dice", s->dice}, {"faces", facesOf(*s)}});
    }
    return out;
}

// An ability by its English name ("name", what the creator sends back), with its text as shown ("label", "summary"...).
json abilityBrief(const ShownNames& names, const std::string& name) {
    const Entry* a = names.en.findByName(Kind::Ability, name);
    const Entry* s = a ? sameEntry(names.shown, *a) : nullptr;
    if (!s) s = a;
    return {{"name", name},
            {"label", s ? s->title : name},
            {"wpCost", a ? a->prop("wp_cost") : std::string()},
            {"summary", s ? firstSentence(s->body) : std::string()},
            {"fields", s ? fieldsJson(s->fields) : json::array()},
            {"body", s ? s->body : std::string()}};
}

json kinJson(const ShownNames& names, const Entry& e) {
    const Entry& s = sameEntry(names.shown, e) ? *sameEntry(names.shown, e) : e;
    json innate = json::array();
    for (const std::string& n : e.list("innate_abilities")) innate.push_back(abilityBrief(names, n));
    const SourceInfo* si = names.shown.source(s.sourceId);
    return {{"key", e.key},           {"title", s.title},   {"canon", e.title}, {"movement", e.prop("movement")}, {"innate", innate},
            {"names", stringsJson(s.list("names").empty() ? e.list("names") : s.list("names"))}, {"body", s.body},
            {"source", si && si->homebrew ? si->label : std::string()}};
}

// A gear option of a set ("broadsword", "D8 silver") as shown: the title of the card it names, else its words translated.
std::string gearShown(const ShownNames& names, const std::string& option) {
    if (names.same()) return option;
    if (const Entry* e = matchGear(names.en, option))
        if (const Entry* s = sameEntry(names.shown, *e)) return s->title;
    return i18n::tr(names.shown.language(), option);
}

json professionJson(const ShownNames& names, const Entry& e) {
    const Entry& s = sameEntry(names.shown, e) ? *sameEntry(names.shown, e) : e;
    json p = {{"key", e.key},
              {"title", s.title},
              {"canon", e.title},
              {"keyAttribute", e.prop("key_attribute")},
              {"schools", stringsJson(e.list("schools"))},
              {"skills", stringsJson(e.list("skills"))},
              {"nicknames", stringsJson(s.list("nicknames").empty() ? e.list("nicknames") : s.list("nicknames"))},
              {"body", s.body},
              {"fields", fieldsJson(s.fields)}};
    // A mage chooses among the schools of magic there are: the ones its own lists name and every skill of the "magic" category, whichever pack
    // brings it. A school with no list of its own gets its skill and what all the other schools have in common.
    std::vector<std::string> schools = e.list("schools");
    json bySchool = json::object();
    for (const std::string& school : schools) bySchool[school] = stringsJson(e.list("skills." + school));
    if (!schools.empty()) {
        std::vector<std::string> common = e.list("skills." + schools.front());
        for (const std::string& school : schools)
            std::erase_if(common, [&](const std::string& skill) { return skill == school || !std::ranges::contains(e.list("skills." + school), skill) || std::ranges::contains(schools, skill); });
        for (const Entry& skill : names.en.entries(Kind::Skill)) {
            if (skill.prop("category") != "magic" || std::ranges::contains(schools, skill.title)) continue;
            schools.push_back(skill.title);
            std::vector<std::string> list = {skill.title};
            list.insert(list.end(), common.begin(), common.end());
            bySchool[skill.title] = stringsJson(list);
        }
        p["schools"] = stringsJson(schools);
    }
    p["skillsBySchool"] = bySchool;
    json heroic = json::array();
    for (const std::string& h : e.list("heroic_abilities")) heroic.push_back(abilityBrief(names, h));
    p["heroic"] = heroic;
    p["magic"] = {{"spells", std::atoi(e.prop("magic.spells").c_str())}, {"tricks", std::atoi(e.prop("magic.tricks").c_str())}, {"rank", std::atoi(e.prop("magic.spell_rank").c_str())}};
    json sets = json::array();
    const auto& shownSets = s.list("starting_gear");
    const auto& englishSets = e.list("starting_gear");
    for (size_t i = 0; i < englishSets.size(); ++i) {
        // the picks are the English set's (what the server builds the sheet from); their words as shown
        json picks = json::array();
        for (const GearPick& g : parseGearSet(englishSets[i])) {
            json options = json::array();
            for (const std::string& o : g.options) options.push_back(gearShown(names, o));
            picks.push_back({{"options", options}, {"diceSides", g.diceSides}, {"what", g.diceSides ? gearShown(names, g.what) : g.what}});
        }
        sets.push_back({{"text", i < shownSets.size() ? shownSets[i] : englishSets[i]}, {"picks", picks}});
    }
    p["gearSets"] = sets;
    const SourceInfo* si = names.shown.source(s.sourceId);
    p["source"] = si && si->homebrew ? si->label : std::string();
    return p;
}

int intOf(const json& o, const char* key, int def) { return jsonInt(o, key, def); }

// The row of `english` whose first cell is `text`, and that row of the same table as shown: the rolled text in the page's language.
std::string sameRow(const DataTable& english, const DataTable& shown, const std::string& text) {
    for (size_t i = 0; i < english.rows.size() && i < shown.rows.size(); ++i)
        if (!english.rows[i].cells.empty() && english.rows[i].cells.front() == text && !shown.rows[i].cells.empty()) return shown.rows[i].cells.front();
    return text;
}

// The same item of a list of a card in the content shown (a kin's first names), by its place in the English list.
std::string sameListItem(const std::vector<std::string>& english, const std::vector<std::string>& shown, const std::string& text) {
    for (size_t i = 0; i < english.size() && i < shown.size(); ++i)
        if (english[i] == text) return shown[i];
    return text;
}

}  // namespace

json creationCatalog(const ContentStore& content, const ContentStore& shown) {
    const ShownNames names{content, shown};
    const std::string& lang = shown.language();
    json c = json::object();
    json kins = json::array(), profs = json::array();
    for (const Entry& e : content.entries(Kind::Kin)) kins.push_back(kinJson(names, e));
    for (const Entry& e : content.entries(Kind::Profession)) profs.push_back(professionJson(names, e));
    c["kins"] = kins;
    c["professions"] = profs;

    json ages = json::array();
    for (const AgeDef& r : content.ages()) {
        json mods = json::array();                             // one per attribute, in the order of "attributes" below
        for (const AttributeDef& a : content.attributes().attributes()) mods.push_back(r.modFor(a.id));
        ages.push_back({{"id", r.id}, {"label", shown.age(r.id).name}, {"summary", shown.ageSummary(shown.age(r.id))}, {"trainedSkills", r.trainedSkills()}, {"attrMod", mods}});
    }
    c["ages"] = ages;

    // "id" is what the skills and professions name their attribute by; "label" (short form) and "long" (name) as shown
    json attrs = json::array();
    for (const AttributeDef& a : content.attributes().attributes())
        attrs.push_back({{"id", a.id}, {"label", shown.text(a.abbrKey)}, {"long", shown.text(a.nameKey)}, {"description", shown.text(a.descriptionKey)}});
    c["attributes"] = attrs;
    json chance = json::array();                               // base chance of a score, index = score
    for (int v = 0; v <= 18; ++v) chance.push_back(baseChance(content.attributes(), v));
    c["baseChance"] = chance;
    c["professionSkills"] = content.age("adult").professionSkills;

    // the skills, abilities and schools go by their English names (what is sent back); "labels" says how each is shown
    json labels = json::object();
    json skillAttr = json::object(), selectable = json::array();
    for (const Entry& e : content.entries(Kind::Skill)) {
        skillAttr[e.title] = e.prop("attribute");
        labels[e.title] = names.show({Kind::Skill}, e.key, e.title);
    }
    for (const Entry* e : selectableSkills(content)) {
        const Entry* s = sameEntry(shown, *e);
        selectable.push_back({{"title", e->title}, {"attribute", e->prop("attribute")}, {"attributeLabel", shown.attributeAbbr(e->prop("attribute"))},
                              {"summary", firstSentence((s ? s : e)->body)}});
    }
    c["skillAttributes"] = skillAttr;
    c["selectableSkills"] = selectable;

    json heroicAll = json::array();
    for (const Entry& a : content.entries(Kind::Ability)) {
        labels[a.title] = names.show({Kind::Ability}, a.key, a.title);
        if (a.prop("type") == "heroic") heroicAll.push_back(abilityBrief(names, a.title));
    }
    c["heroicAbilities"] = heroicAll;

    json spells = json::array();
    for (const Entry& s : content.entries(Kind::Spell)) {
        const Entry* shownSpell = sameEntry(shown, s);
        const std::string school = s.prop("school");
        if (!labels.contains(school)) labels[school] = school == "General Magic" ? i18n::tr(lang, school) : names.show({Kind::Skill}, "", school);
        spells.push_back({{"key", s.key}, {"title", (shownSpell ? shownSpell : &s)->title}, {"school", school}, {"trick", s.prop("trick") == "1"},
                          {"rank", std::atoi(s.prop("rank").c_str())}, {"summary", firstSentence((shownSpell ? shownSpell : &s)->body)}});
    }
    c["spells"] = spells;
    c["labels"] = labels;

    c["tables"] = {{"kin", bookFaces(content, "Kin")},
                   {"profession", bookFaces(content, "Profession")},
                   {"weakness", rolesJson(content, shown, "weakness")},
                   {"memento", rolesJson(content, shown, "memento")},
                   {"appearance", rolesJson(content, shown, "appearance")}};
    return c;
}

void localizeCreation(Creation& c, const ContentStore& content, const ContentStore& shown) {
    if (&content == &shown) return;
    for (const auto& [role, field] : {std::pair<const char*, std::string*>{"weakness", &c.weakness}, {"memento", &c.memento}, {"appearance", &c.appearance}})
        for (const DataTable* t : content.tablesByRole(role)) {
            const std::string was = *field;
            *field = sameRow(*t, *shownTable(shown, *t), *field);
            if (*field != was) break;
        }
    if (const Entry* kin = content.entry(Kind::Kin, content.idByKey(Kind::Kin, c.kinKey)))
        if (const Entry* s = sameEntry(shown, *kin)) c.name = sameListItem(kin->list("names"), s->list("names"), c.name);
    if (const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, c.professionKey)))
        if (const Entry* s = sameEntry(shown, *prof)) c.nickname = sameListItem(prof->list("nicknames"), s->list("nicknames"), c.nickname);
}

Creation creationFromJson(const json& body, const ContentStore& content) {
    Creation c;
    auto known = [&](Kind k, const std::string& key) { return content.idByKey(k, key) != 0 ? key : std::string(); };
    c.kinKey = known(Kind::Kin, jsonStr(body, "kin"));
    c.professionKey = known(Kind::Profession, jsonStr(body, "profession"));
    c.age = content.age(jsonStr(body, "age", "adult")).id;
    c.school = jsonStr(body, "school");
    c.rolled.assign(content.attributes().attributes().size(), 0);                       // one score per attribute, in the catalog's order
    if (const json* r = jsonFind(body, "rolled"); r && r->is_array())
        for (size_t i = 0; i < r->size() && i < c.rolled.size(); ++i)
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
    for (int v : c.rolled) rolled.push_back(v);                 // (in the order of the content's attributes)
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

json creationPreview(const Creation& c, const ContentStore& content, const ContentStore& shown, Dice& dice) {
    const ShownNames names{content, shown};
    const auto name = [&](const std::string& n) { return names.showAny(n); };
    json problems = json::array();
    for (const std::string& p : validateCreation(c, content)) problems.push_back(i18n::tr(shown.language(), p, name));
    json out = {{"problems", problems}};

    const AttributeSet& rules = content.attributes();
    std::vector<int> rolledScores;
    for (size_t i = 0; i < rules.attributes().size(); ++i) rolledScores.push_back(std::max(3, i < c.rolled.size() ? c.rolled[i] : 0));
    Character sheet;
    sheet.attr = applyAge(rules, rolledScores, content.age(c.age));
    const Entry* kin = content.entry(Kind::Kin, content.idByKey(Kind::Kin, c.kinKey));
    const int move = kin ? std::atoi(kin->prop("movement").c_str()) : 0;
    json attrs = json::array();                                // the scores after the age, in the order of the catalog's attributes
    for (const AttributeDef& a : rules.attributes()) attrs.push_back(sheet.attr.get(a.id));
    out["attributes"] = attrs;
    json bonuses = json::array();                              // the damage bonus of each attribute the rules give one: {key, short, value}
    if (const Lookup* bonus = rules.lookup("damage-bonus"))
        for (const std::string& id : bonus->attributes)
            bonuses.push_back({{"key", id}, {"short", shown.attributeAbbr(id)}, {"value", damageBonus(rules, sheet.attr.get(id))}});
    out["derived"] = {{"hp", maxHp(sheet)},
                      {"wp", maxWp(sheet)},
                      {"movement", move ? json(move + movementModifier(rules, sheet.attr.get(kAgility))) : json(nullptr)},
                      {"damageBonus", bonuses},
                      {"encumbrance", encumbranceLimit(sheet)}};
    if (problems.empty()) {
        // the sheet as it will start, in the page's language: its names as shown (the file keeps them in English)
        Character built = buildCharacter(c, content, dice);
        json doc;
        jsonParse(built.toJson(), doc, nullptr);
        Character::fromJson(names.showDoc(doc).dump(), built, nullptr);
        out["summary"] = built.summary(rules, [&](const std::string& id) { return shown.attributeAbbr(id); }, shown.age(built.age).name);
    } else {
        out["summary"] = std::string();
    }
    return out;
}

}  // namespace gm
