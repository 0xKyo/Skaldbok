#include "game/creation.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <set>
#include <utility>

#include "parsing/fts.h"

namespace gm {
namespace {

std::string capitalized(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string::npos) end = s.size();
        const std::string part = trimmed(s.substr(start, end - start));
        if (!part.empty()) out.push_back(part);
        start = end + 1;
    }
    return out;
}

// Words of a gear name, lowercase, punctuation gone; a parenthesis with a number in it ("(10 meters)") is dropped
// and so is the word "armor" ("leather armor" = "Leather").
std::vector<std::string> words(const std::string& name) {
    std::string clean;
    int depth = 0;
    std::string paren;
    for (unsigned char c : name) {
        if (c == '(') {
            depth = 1;
            paren.clear();
        } else if (c == ')' && depth) {
            depth = 0;
            bool digit = false;
            for (char p : paren) digit |= std::isdigit(static_cast<unsigned char>(p)) != 0;
            if (!digit) clean += " " + paren + " ";
        } else if (depth) {
            paren += static_cast<char>(std::tolower(c));
        } else if (std::isalnum(c)) {
            clean += static_cast<char>(std::tolower(c));
        } else {
            clean += ' ';
        }
    }
    std::vector<std::string> out;
    std::string w;
    for (char c : clean + " ") {
        if (c == ' ') {
            if (!w.empty() && w != "armor" && w != "s") out.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    return out;
}

std::string keyUnordered(std::vector<std::string> w) {
    std::sort(w.begin(), w.end());
    std::string out;
    for (const std::string& s : w) out += s + " ";
    return out;
}

std::string keyJoined(const std::vector<std::string>& w) {
    std::string out;
    for (const std::string& s : w) out += s;
    return out;
}

Ref refOf(const Entry* e, const std::string& fallbackName) {
    Ref r;
    r.key = e ? e->key : std::string();
    r.name = e ? e->title : fallbackName;
    return r;
}

}  // namespace

// ------------------------------------------------------------------------------------------ gear

std::vector<GearPick> parseGearSet(const std::string& text) {
    std::vector<GearPick> out;
    for (const std::string& token : splitOn(text, ',')) {
        GearPick p;
        if (token.size() > 2 && (token[0] == 'D' || token[0] == 'd') && std::isdigit(static_cast<unsigned char>(token[1]))) {
            size_t i = 1;
            int sides = 0;
            while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) sides = sides * 10 + (token[i++] - '0');
            const std::string rest = trimmed(token.substr(i));
            if (sides > 1 && !rest.empty()) {
                p.diceSides = sides;
                p.what = rest;
                p.options = {token};
                out.push_back(std::move(p));
                continue;
            }
        }
        p.options = splitOn(token, '/');
        if (p.options.empty()) continue;
        out.push_back(std::move(p));
    }
    return out;
}

int gearSetForRoll(int d6) { return d6 <= 2 ? 0 : d6 <= 4 ? 1 : 2; }

const Entry* matchGear(const ContentStore& content, const std::string& name) {
    const std::vector<std::string> w = words(name);
    if (w.empty()) return nullptr;
    const std::string unordered = keyUnordered(w), joined = keyJoined(w);
    for (Kind k : {Kind::Weapon, Kind::Armor, Kind::Gear})
        for (const Entry& e : content.entries(k)) {
            const std::vector<std::string> ew = words(e.title);
            if (keyUnordered(ew) == unordered || keyJoined(ew) == joined) return &e;
        }
    return nullptr;
}

int rollAttribute(Dice& dice) {
    int lowest = 7, sum = 0;
    for (int i = 0; i < 4; ++i) {
        const int d = dice.roll(6);
        sum += d;
        lowest = std::min(lowest, d);
    }
    return sum - lowest;
}

std::string rollTable(const DataTable& table, Dice& dice, int* rollOut) {
    if (table.rows.empty()) return {};
    const int sides = table.dieSides() > 0 ? table.dieSides() : static_cast<int>(table.rows.size());
    const int roll = dice.roll(sides);
    if (rollOut) *rollOut = roll;
    int row = table.rowForRoll(roll);
    if (row < 0) row = std::min(roll, static_cast<int>(table.rows.size())) - 1;
    const auto& cells = table.rows[static_cast<size_t>(row)].cells;
    return cells.empty() ? std::string() : cells.front();
}

// ------------------------------------------------------------------------------------------ skills

std::vector<std::string> professionSkillOptions(const Entry& profession, const std::string& school) {
    if (!profession.list("schools").empty()) {
        if (school.empty()) return {};
        return profession.list("skills." + school);
    }
    return profession.list("skills");
}

std::vector<const Entry*> selectableSkills(const ContentStore& content) {
    std::vector<const Entry*> out;
    for (const Entry& e : content.entries(Kind::Skill))
        if (attrIndex(e.prop("attribute")) >= 0 && e.prop("category") != "magic") out.push_back(&e);   // schools come with the mage profession
    return out;
}

std::vector<const Entry*> magicChoices(const ContentStore& content, const Entry& profession, const std::string& school, bool tricks) {
    std::vector<const Entry*> out;
    const int rank = std::atoi(profession.prop("magic.spell_rank").c_str());
    for (const Entry& e : content.entries(Kind::Spell)) {
        const bool isTrick = e.prop("trick") == "1";
        if (isTrick != tricks) continue;
        const std::string s = e.prop("school");
        if (s != school && s != "General Magic") continue;
        if (!tricks && rank > 0 && std::atoi(e.prop("rank").c_str()) != rank) continue;
        out.push_back(&e);
    }
    return out;
}

// ------------------------------------------------------------------------------------- validation

std::vector<std::string> validateCreation(const Creation& c, const ContentStore& content) {
    std::vector<std::string> problems;
    const Entry* kin = content.entry(Kind::Kin, content.idByKey(Kind::Kin, c.kinKey));
    const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, c.professionKey));
    if (!kin) problems.push_back("Choose a kin.");
    if (!prof) problems.push_back("Choose a profession.");
    for (int i = 0; i < kAttrCount; ++i)
        if (c.rolled[i] < 3 || c.rolled[i] > 18) {
            problems.push_back("Give every attribute a score from 3 to 18.");
            break;
        }
    if (!prof) return problems;

    const bool magic = !prof->list("schools").empty();
    if (magic) {
        const auto& schools = prof->list("schools");
        if (!std::ranges::contains(schools, c.school)) problems.push_back("Choose a school of magic.");
    }
    const std::vector<std::string> options = professionSkillOptions(*prof, c.school);
    const size_t needProf = std::min<size_t>(kProfessionSkills, options.size());
    std::set<std::string> seen;
    bool badProfSkill = false;
    for (const std::string& s : c.professionSkills) {
        badProfSkill |= !std::ranges::contains(options, s);
        seen.insert(lowerCopy(s));
    }
    if (c.professionSkills.size() != needProf || badProfSkill || seen.size() != c.professionSkills.size())
        problems.push_back("Choose " + std::to_string(needProf) + " trained skills from the profession's list.");
    if (magic && !c.school.empty() &&
        !std::ranges::contains(c.professionSkills, c.school))
        problems.push_back("The school of magic must be one of the trained skills.");
    const int freeWanted = ageRule(c.age).trainedSkills - static_cast<int>(needProf);
    bool badFree = false;
    for (const std::string& s : c.freeSkills) {
        const Entry* e = content.findByName(Kind::Skill, s);
        badFree |= !e || attrIndex(e->prop("attribute")) < 0 || e->prop("category") == "magic" || seen.count(lowerCopy(s));
        seen.insert(lowerCopy(s));
    }
    if (static_cast<int>(c.freeSkills.size()) != freeWanted || badFree)
        problems.push_back("Choose " + std::to_string(freeWanted) + " more trained skills of your own (no repeats).");

    if (magic) {
        const int spells = std::atoi(prof->prop("magic.spells").c_str()), tricks = std::atoi(prof->prop("magic.tricks").c_str());
        int gotSpells = 0, gotTricks = 0;
        for (const std::string& key : c.spells) {
            const Entry* e = content.entry(Kind::Spell, content.idByKey(Kind::Spell, key));
            if (!e) continue;
            (e->prop("trick") == "1" ? gotTricks : gotSpells)++;
        }
        if (gotSpells != spells || gotTricks != tricks)
            problems.push_back("Choose " + std::to_string(spells) + " spells and " + std::to_string(tricks) + " magic tricks.");
    } else if (!prof->list("heroic_abilities").empty() && c.heroicAbility.empty()) {
        problems.push_back("Choose the heroic ability.");
    }
    if (trimmed(c.name).empty()) problems.push_back("Give the character a name.");
    return problems;
}

void chooseGearSet(Creation& c, const Entry& profession, int set, Dice& dice) {
    const auto& sets = profession.list("starting_gear");
    if (set < 0 || set >= static_cast<int>(sets.size())) {
        c.gearSet = -1;
        c.gear.clear();
        return;
    }
    c.gearSet = set;
    c.gear = parseGearSet(sets[static_cast<size_t>(set)]);
    for (GearPick& p : c.gear)
        if (p.diceSides > 0) p.rolled = dice.roll(p.diceSides);
}

Creation randomCreation(const ContentStore& content, Dice& dice) {
    auto pickOf = [&](size_t n) { return static_cast<size_t>(dice.roll(static_cast<int>(n)) - 1); };
    Creation c;
    const auto& kins = content.entries(Kind::Kin);
    const auto& profs = content.entries(Kind::Profession);
    if (kins.empty() || profs.empty()) return c;
    const Entry& kin = kins[pickOf(kins.size())];
    const Entry& prof = profs[pickOf(profs.size())];
    c.kinKey = kin.key;
    c.professionKey = prof.key;
    const int d6 = dice.roll(6);
    c.age = d6 <= 3 ? "young" : d6 <= 5 ? "adult" : "old";

    const auto& schools = prof.list("schools");
    if (!schools.empty()) c.school = schools[pickOf(schools.size())];

    // six 4D6 scores; the best goes to the profession's key attribute, the others to random attributes
    std::vector<int> scores;
    for (int i = 0; i < kAttrCount; ++i) scores.push_back(rollAttribute(dice));
    std::ranges::sort(scores, std::greater<int>());
    std::vector<int> slots;
    const int key = std::max(0, attrIndex(prof.prop("key_attribute")));
    for (int i = 0; i < kAttrCount; ++i)
        if (i != key) slots.push_back(i);
    for (size_t i = slots.size(); i > 1; --i) std::swap(slots[i - 1], slots[pickOf(i)]);
    c.rolled[key] = scores[0];
    for (size_t i = 0; i < slots.size(); ++i) c.rolled[slots[i]] = scores[i + 1];

    std::vector<std::string> options = professionSkillOptions(prof, c.school);
    if (!schools.empty()) {
        c.professionSkills.push_back(c.school);
        std::erase(options, c.school);
    }
    const size_t need = std::min<size_t>(kProfessionSkills, professionSkillOptions(prof, c.school).size());
    while (c.professionSkills.size() < need && !options.empty()) {
        const size_t i = pickOf(options.size());
        c.professionSkills.push_back(options[i]);
        options.erase(options.begin() + static_cast<std::ptrdiff_t>(i));
    }
    std::vector<const Entry*> free = selectableSkills(content);
    std::erase_if(free, [&](const Entry* e) {
                                  return std::ranges::contains(c.professionSkills, e->title);
                              });
    const size_t wanted = static_cast<size_t>(std::max(0, ageRule(c.age).trainedSkills - static_cast<int>(need)));
    while (c.freeSkills.size() < wanted && !free.empty()) {
        const size_t i = pickOf(free.size());
        c.freeSkills.push_back(free[i]->title);
        free.erase(free.begin() + static_cast<std::ptrdiff_t>(i));
    }

    if (!schools.empty()) {
        for (bool tricks : {false, true}) {
            auto choices = magicChoices(content, prof, c.school, tricks);
            const size_t n = static_cast<size_t>(std::atoi(prof.prop(tricks ? "magic.tricks" : "magic.spells").c_str()));
            for (size_t k = 0; k < n && !choices.empty(); ++k) {
                const size_t i = pickOf(choices.size());
                c.spells.push_back(choices[i]->key);
                choices.erase(choices.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
    } else if (!prof.list("heroic_abilities").empty()) {
        const auto& heroics = prof.list("heroic_abilities");
        c.heroicAbility = heroics[pickOf(heroics.size())];
    }

    if (!prof.list("starting_gear").empty()) {
        chooseGearSet(c, prof, gearSetForRoll(dice.roll(6)), dice);
        for (GearPick& p : c.gear)
            if (p.options.size() > 1) p.choice = static_cast<int>(pickOf(p.options.size()));
    }

    const auto& names = kin.list("names");
    c.name = names.empty() ? "Nameless" : names[pickOf(names.size())];
    const auto& nicknames = prof.list("nicknames");
    if (!nicknames.empty() && dice.roll(2) == 1) c.nickname = nicknames[pickOf(nicknames.size())];
    const std::pair<const char*, std::string*> rolled[] = {{"weakness", &c.weakness}, {"memento", &c.memento}, {"appearance", &c.appearance}};
    for (const auto& [role, field] : rolled)
        if (const DataTable* t = content.tableByRole(role))
            if (std::string(role) != "weakness" || dice.roll(2) == 1) *field = rollTable(*t, dice);   // the weakness is optional
    return c;
}

// ---------------------------------------------------------------------------------------- building

Character buildCharacter(const Creation& cr, const ContentStore& content, Dice& dice) {
    Character c;
    const Entry* kin = content.entry(Kind::Kin, content.idByKey(Kind::Kin, cr.kinKey));
    const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, cr.professionKey));
    c.name = trimmed(cr.name);
    c.nickname = trimmed(cr.nickname);
    c.player = trimmed(cr.player);
    c.age = ageRule(cr.age).id;
    c.kin = refOf(kin, "");
    c.profession = refOf(prof, "");
    c.school = cr.school;
    applyAge(cr.rolled, c.age, c.attr);
    c.hp = maxHp(c);
    c.wp = maxWp(c);
    c.weakness = cr.weakness;
    c.memento = cr.memento;
    c.appearance = cr.appearance;

    // skills: a trained skill starts at twice its base chance
    auto addSkill = [&](const std::string& name) {
        const Entry* s = content.findByName(Kind::Skill, name);
        SkillEntry e;
        e.ref = refOf(s, name);
        e.attribute = s ? s->prop("attribute") : std::string();
        e.trained = true;
        e.level = std::min(18, 2 * baseChance(c.attr[std::max(0, attrIndex(e.attribute))]));
        c.skills.push_back(std::move(e));
    };
    for (const std::string& s : cr.professionSkills) addSkill(s);
    for (const std::string& s : cr.freeSkills) addSkill(s);

    // abilities: the kin's innate ones, and the chosen heroic ability
    if (kin)
        for (const std::string& n : kin->list("innate_abilities")) c.abilities.push_back(refOf(content.findByName(Kind::Ability, n), n));
    if (!cr.heroicAbility.empty()) c.abilities.push_back(refOf(content.findByName(Kind::Ability, cr.heroicAbility), cr.heroicAbility));
    for (const std::string& key : cr.spells) {
        const Entry* e = content.entry(Kind::Spell, content.idByKey(Kind::Spell, key));
        if (e) c.spells.push_back(refOf(e, ""));
    }

    // gear
    std::vector<GearPick> picks = cr.gear;
    for (GearPick& p : picks) {
        if (p.diceSides > 0) {
            const int n = p.rolled > 0 ? p.rolled : dice.roll(p.diceSides);
            const std::string what = lowerCopy(p.what);
            if (what == "silver") c.silver += n;
            else if (what == "gold") c.gold += n;
            else if (what == "copper") c.copper += n;
            else {
                Item it;
                it.name = capitalized(p.what);
                it.count = n;
                c.inventory.push_back(std::move(it));
            }
            continue;
        }
        const std::string text = p.options[static_cast<size_t>(std::clamp(p.choice, 0, static_cast<int>(p.options.size()) - 1))];
        const Entry* e = matchGear(content, text);
        Item it;
        it.name = e ? e->title : capitalized(text);
        it.key = e ? e->key : std::string();
        if (e && e->kind == Kind::Armor && e->prop("slot") == "helmet" && c.helmet.name.empty()) c.helmet = it;
        else if (e && e->kind == Kind::Armor && c.armor.name.empty()) c.armor = it;
        else if (e && e->kind == Kind::Weapon && c.weapons.size() < 3) c.weapons.push_back(it);
        else c.inventory.push_back(it);
    }
    return c;
}

}  // namespace gm
