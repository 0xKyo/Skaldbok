// Characters: the Rulebook's numbers, the creation rules, gear matching, and the JSON files.
#include <algorithm>
#include <string>
#include <vector>

#include "character.h"
#include "content.h"
#include "creation.h"
#include "db.h"
#include "dice.h"
#include "packs.h"
#include "party.h"
#include "testutil.h"

using namespace gm;
using test::check;

namespace {

bool hasSkill(const Character& c, const std::string& name) {
    for (const SkillEntry& s : c.skills)
        if (s.ref.name == name) return true;
    return false;
}
const SkillEntry* skill(const Character& c, const std::string& name) {
    for (const SkillEntry& s : c.skills)
        if (s.ref.name == name) return &s;
    return nullptr;
}
bool hasAbility(const Character& c, const std::string& name) {
    for (const Ref& r : c.abilities)
        if (r.name == name) return true;
    return false;
}
bool problemMentions(const std::vector<std::string>& p, const std::string& what) {
    for (const std::string& s : p)
        if (s.contains(what)) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dataDir = test::dataDir(argc, argv);
    Database db;
    std::string err;
    if (!db.open(dataDir + "/dragonbane.db", &err)) {
        std::printf("cannot open database: %s\n", err.c_str());
        return 2;
    }
    ContentStore content;
    content.load(db, {PackSpec{dataDir + "/packs/core", true, true},
                      PackSpec{test::sourceDir() + "/examples/frostmarch-tales", false, true}});
    Dice dice;

    // -------------------------------------------------------------------------------- the numbers
    check(baseChance(1) == 3 && baseChance(5) == 3 && baseChance(6) == 4 && baseChance(8) == 4 && baseChance(9) == 5 && baseChance(12) == 5 &&
              baseChance(13) == 6 && baseChance(15) == 6 && baseChance(16) == 7 && baseChance(18) == 7,
          "base chance table (p.29)");
    check(movementModifier(6) == -4 && movementModifier(7) == -2 && movementModifier(9) == -2 && movementModifier(10) == 0 &&
              movementModifier(12) == 0 && movementModifier(13) == 2 && movementModifier(16) == 4,
          "movement modifier by AGL");
    check(damageBonus(12).empty() && damageBonus(13) == "D4" && damageBonus(16) == "D4" && damageBonus(17) == "D6" && damageBonus(18) == "D6",
          "damage bonus: none up to 12, D4 13-16, D6 17+");
    check(ageRule("young").trainedSkills == 8 && ageRule("adult").trainedSkills == 10 && ageRule("old").trainedSkills == 12 &&
              std::string(ageRule("nonsense").id) == "adult",
          "age: 8 / 10 / 12 trained skills, unknown = adult");
    int rolled[6] = {18, 10, 17, 9, 3, 12}, out[6];
    applyAge(rolled, "young", out);
    check(out[0] == 18 && out[1] == 11 && out[2] == 18 && out[3] == 9, "young: AGL and CON +1, never above 18");
    applyAge(rolled, "old", out);
    check(out[0] == 16 && out[1] == 8 && out[2] == 15 && out[3] == 10 && out[4] == 4 && out[5] == 12, "old: STR AGL CON -2, INT WIL +1");
    applyAge(rolled, "adult", out);
    check(std::equal(out, out + 6, rolled), "adult: no change");
    bool inRange = true;
    for (int i = 0; i < 3000; ++i) {
        const int a = rollAttribute(dice);
        inRange &= a >= 3 && a <= 18;
    }
    check(inRange, "4D6 drop lowest stays within 3..18");
    Character sheet;
    sheet.attr[0] = 13;
    sheet.attr[1] = 11;
    sheet.attr[4] = 9;
    check(encumbranceLimit(sheet) == 7 && maxHp(sheet) == 11 && maxWp(sheet) == 9, "encumbrance is half STR rounded up; HP = CON, WP = WIL");
    sheet.inventory.push_back({"Backpack", "", 1, ""});
    sheet.hpBonus = 2;
    check(encumbranceLimit(sheet) == 9 && maxHp(sheet) == 13, "a backpack adds 2; Robust-type bonuses add to HP");

    // ------------------------------------------------------------------------------- gear
    auto pick = [&](const char* n) { return matchGear(content, n); };
    check(pick("small shield") && pick("small shield")->title == "Shield, Small", "'small shield' is 'Shield, Small'");
    check(pick("morning star") && pick("morning star")->title == "Morningstar", "'morning star' is 'Morningstar'");
    check(pick("battle axe") && pick("battle axe")->title == "Battleaxe", "'battle axe' is 'Battleaxe'");
    check(pick("leather armor") && pick("leather armor")->title == "Leather" && pick("leather armor")->kind == Kind::Armor, "'leather armor' is the armor 'Leather'");
    check(pick("studded leather armor") && pick("studded leather armor")->title == "Studded Leather", "'studded leather armor'");
    check(pick("light crossbow") && pick("light crossbow")->title == "Crossbow, Light", "'light crossbow'");
    check(pick("lockpicks (simple)") && pick("lockpicks (simple)")->title == "Lockpicks, Simple", "'lockpicks (simple)'");
    check(pick("rope (hemp)") && pick("rope (hemp)")->title.rfind("Rope, Hemp", 0) == 0, "'rope (hemp)' ignores the '(10 meters)'");
    check(pick("flint & tinder") && pick("torch") && pick("Knife")->kind == Kind::Weapon, "flint & tinder, torch, knife");
    check(!pick("sleeping pelt") && !pick("nonsense thing"), "unknown gear is simply not matched");
    check(pick("open helmet") && pick("open helmet")->prop("slot") == "helmet", "'open helmet' is a helmet");
    check(pick("snow goggles") && pick("snow goggles")->key == "frostmarch/gear/snow-goggles", "homebrew gear is matched too");

    auto set = parseGearSet("Broadsword/battle axe/morning star, small shield, chainmail, torch, D6 food rations, D6 silver");
    check(set.size() == 6 && set[0].options.size() == 3 && set[4].diceSides == 6 && set[4].what == "food rations" && set[5].what == "silver",
          "gear set parsing: choices and dice");
    check(gearSetForRoll(1) == 0 && gearSetForRoll(2) == 0 && gearSetForRoll(3) == 1 && gearSetForRoll(4) == 1 && gearSetForRoll(5) == 2 &&
              gearSetForRoll(6) == 2,
          "D6 picks one of the three gear sets");

    // ------------------------------------------------------------------- a Human fighter, by the book
    const Entry* fighter = content.findByName(Kind::Profession, "Fighter");
    Creation cr;
    cr.kinKey = content.findByName(Kind::Kin, "Human")->key;
    cr.professionKey = fighter->key;
    cr.age = "adult";
    int assign[6] = {14, 12, 10, 9, 8, 13};
    std::copy(assign, assign + 6, cr.rolled);
    check(problemMentions(validateCreation(cr, content), "trained skills from the profession"), "missing profession skills are reported");
    cr.professionSkills = {"Axes", "Bows", "Brawling", "Crossbows", "Evade", "Swords"};
    cr.freeSkills = {"Awareness", "Healing", "Riding"};
    check(problemMentions(validateCreation(cr, content), "4 more"), "an adult needs 4 skills of their own");
    cr.freeSkills.push_back("Swimming");
    cr.heroicAbility = "Veteran";
    check(problemMentions(validateCreation(cr, content), "name"), "a name is needed");
    cr.name = "Brenna";
    cr.nickname = "Grimjaw";
    cr.gearSet = 0;
    cr.gear = parseGearSet(fighter->list("starting_gear")[0]);
    cr.gear[0].choice = 2;                                      // the morning star
    check(validateCreation(cr, content).empty(), "a complete fighter is valid");

    Character c = buildCharacter(cr, content, dice);
    check(c.name == "Brenna" && c.displayName() == "Brenna \"Grimjaw\"" && c.kin.name == "Human" && c.profession.name == "Fighter", "name, kin, profession");
    check(std::equal(c.attr, c.attr + 6, assign) && c.hp == 12 && c.wp == 8, "adult: attributes as rolled, HP = CON 12, WP = WIL 8");
    check(c.skills.size() == 10, "10 trained skills for an adult");
    check(skill(c, "Axes") && skill(c, "Axes")->level == 12 && skill(c, "Axes")->attribute == "STR", "Axes (STR 14): base chance 6, trained = 12");
    check(skill(c, "Evade") && skill(c, "Evade")->level == 10, "Evade (AGL 10): base chance 5, trained = 10");
    check(skill(c, "Awareness") && skill(c, "Awareness")->level == 10 && skill(c, "Healing")->level == 10, "own skills are trained too");
    check(hasAbility(c, "Adaptive") && hasAbility(c, "Veteran") && c.abilities.size() == 2, "innate ability of the kin + heroic ability of the profession");
    check(c.abilities[0].key == "core/ability/adaptive", "abilities are stored as stable keys");
    check(c.weapons.size() >= 2 && c.weapons[0].name == "Morningstar" && c.weapons[1].name == "Shield, Small", "chosen weapon and shield are at hand");
    check(c.armor.name == "Chainmail" && c.helmet.name.empty(), "chainmail goes in the armor slot");
    bool torch = false;
    int rations = 0;
    for (const Item& it : c.inventory) {
        torch |= it.name == "Torch";
        if (it.name == "Food rations") rations = it.count;
    }
    check(torch && rations >= 1 && rations <= 6 && c.silver >= 1 && c.silver <= 6, "torch, D6 rations and D6 silver end up in the inventory");
    check(skillLevel(c, nullptr, "STR") == 6 && skillLevel(c, skill(c, "Axes"), "STR") == 12, "untrained skill = base chance; trained = its level");
    check(c.hp == maxHp(c) && damageBonus(c.attr[0]) == "D4" && movementModifier(c.attr[2]) == 0, "derived: STR 14 gives D4 damage bonus, AGL 10 no movement change");

    // young and old change attributes and skill counts
    Creation young = cr;
    young.age = "young";
    young.freeSkills = {"Awareness", "Healing"};
    check(validateCreation(young, content).empty(), "a young character needs only 2 skills of their own");
    Character yc = buildCharacter(young, content, dice);
    check(yc.attr[1] == 13 && yc.attr[2] == 11 && yc.skills.size() == 8, "young: CON+1, AGL+1, 8 trained skills");
    Creation elder = cr;
    elder.age = "old";
    elder.freeSkills = {"Awareness", "Healing", "Riding", "Swimming", "Sneaking", "Bushcraft"};
    check(validateCreation(elder, content).empty(), "an old character picks 6 skills of their own");
    Character oc = buildCharacter(elder, content, dice);
    check(oc.attr[0] == 12 && oc.attr[1] == 10 && oc.attr[2] == 8 && oc.attr[3] == 10 && oc.attr[4] == 9 && oc.skills.size() == 12,
          "old: STR/CON/AGL -2, INT/WIL +1, 12 trained skills");

    // things the rules forbid
    Creation bad = cr;
    bad.professionSkills = {"Axes", "Bows", "Brawling", "Crossbows", "Evade", "Healing"};       // Healing is not a Fighter skill
    check(problemMentions(validateCreation(bad, content), "profession's list"), "a profession skill outside the profession's list is refused");
    bad = cr;
    bad.freeSkills = {"Axes", "Healing", "Riding", "Swimming"};                                   // Axes twice
    check(problemMentions(validateCreation(bad, content), "no repeats"), "the same skill twice is refused");
    bad = cr;
    bad.rolled[0] = 19;
    check(problemMentions(validateCreation(bad, content), "3 to 18"), "an attribute over 18 is refused");
    bad = cr;
    bad.heroicAbility.clear();
    check(problemMentions(validateCreation(bad, content), "heroic ability"), "a heroic ability is required");
    bad = cr;
    bad.freeSkills = {"Awareness", "Healing", "Riding", "Weapon Skills"};                         // the heading, not a skill
    check(!validateCreation(bad, content).empty(), "the 'Weapon Skills' heading is not a skill");

    // ------------------------------------------------------------------------------------- a mage
    const Entry* mageP = content.findByName(Kind::Profession, "Mage");
    Creation mg;
    mg.kinKey = content.findByName(Kind::Kin, "Elf")->key;
    mg.professionKey = mageP->key;
    mg.age = "adult";
    int mageAttr[6] = {8, 10, 12, 14, 13, 9};
    std::copy(mageAttr, mageAttr + 6, mg.rolled);
    mg.name = "Elowen";
    check(problemMentions(validateCreation(mg, content), "school"), "a mage must pick a school");
    mg.school = "Animism";
    mg.professionSkills = {"Animism", "Beast Lore", "Bushcraft", "Evade", "Healing", "Sneaking"};
    mg.freeSkills = {"Awareness", "Riding", "Swimming", "Persuasion"};
    check(problemMentions(validateCreation(mg, content), "3 spells and 3 magic tricks"), "a mage must pick 3 spells and 3 tricks");
    auto spells = magicChoices(content, *mageP, "Animism", false);
    auto tricks = magicChoices(content, *mageP, "Animism", true);
    bool onlyRank1 = !spells.empty(), onlySchool = true;
    for (const Entry* e : spells) {
        onlyRank1 &= e->prop("rank") == "1";
        onlySchool &= e->prop("school") == "Animism" || e->prop("school") == "General Magic";
    }
    check(onlyRank1 && onlySchool && tricks.size() >= 3, "spell choices: rank 1, from the school or General Magic");
    for (int i = 0; i < 3; ++i) {
        mg.spells.push_back(spells[static_cast<size_t>(i)]->key);
        mg.spells.push_back(tricks[static_cast<size_t>(i)]->key);
    }
    check(validateCreation(mg, content).empty(), "a complete mage is valid without a heroic ability");
    Character mc = buildCharacter(mg, content, dice);
    check(mc.spells.size() == 6 && mc.school == "Animism" && hasSkill(mc, "Animism") && skill(mc, "Animism")->attribute == "INT" &&
              skill(mc, "Animism")->level == 2 * baseChance(14),
          "mage: 6 spells, the school is a trained INT skill at twice its base chance");
    check(hasAbility(mc, "Inner Peace") && mc.abilities.size() == 1, "the elf has Inner Peace and no heroic ability");
    Creation noTricks = mg;
    noTricks.spells.resize(4);
    check(problemMentions(validateCreation(noTricks, content), "3 spells and 3 magic tricks"), "fewer than 3 spells and 3 tricks is refused");

    // ---------------------------------------------------------------- homebrew in the creator
    const Entry* skald = content.findByName(Kind::Profession, "Skald");
    Creation hb;
    hb.kinKey = content.findByName(Kind::Kin, "Frostborn")->key;
    hb.professionKey = skald->key;
    std::copy(assign, assign + 6, hb.rolled);
    hb.professionSkills = {"Performance", "Persuasion", "Myths & Legends", "Languages", "Swords", "Ice Lore"};
    hb.freeSkills = {"Awareness", "Evade", "Healing", "Riding"};
    hb.heroicAbility = "Icebreaker";
    hb.name = "Skjold";
    check(validateCreation(hb, content).empty(), "homebrew kin + profession + a homebrew skill are valid");
    auto selectable = selectableSkills(content);
    check(std::ranges::any_of(selectable, [](const Entry* e) { return e->title == "Ice Lore"; }) &&
              std::ranges::none_of(selectable, [](const Entry* e) { return e->title == "Weapon Skills"; }),
          "homebrew skills can be picked, headings cannot");
    Character hc = buildCharacter(hb, content, dice);
    check(hasAbility(hc, "Winter's Hide") && hasAbility(hc, "Icebreaker") && hc.abilities[1].key == "frostmarch/ability/icebreaker", "homebrew abilities are stored with their pack's key");
    check(hc.kin.key == "frostmarch/kin/frostborn" && skill(hc, "Ice Lore") && skill(hc, "Ice Lore")->level == 2 * baseChance(9), "homebrew kin and skill keys");

    // random characters, made by the book's own rolls, must always satisfy the rules
    {
        int invalid = 0, wrongSkills = 0, wrongHp = 0, noName = 0, mages = 0;
        std::string firstProblem;
        for (int i = 0; i < 500; ++i) {
            Creation r = randomCreation(content, dice);
            const auto problems = validateCreation(r, content);
            if (!problems.empty()) {
                ++invalid;
                if (firstProblem.empty()) firstProblem = problems.front() + " [" + r.professionKey + ", " + r.age + "]";
                continue;
            }
            Character rc = buildCharacter(r, content, dice);
            wrongSkills += static_cast<int>(rc.skills.size()) != ageRule(rc.age).trainedSkills;
            wrongHp += rc.hp != rc.attr[1] || rc.wp != rc.attr[4];
            noName += rc.name.empty();
            mages += !rc.spells.empty();
            for (int a = 0; a < 6; ++a) wrongHp += rc.attr[a] < 1 || rc.attr[a] > 18;
        }
        check(invalid == 0, "500 random characters are all valid" + (firstProblem.empty() ? std::string() : ": " + firstProblem));
        check(wrongSkills == 0 && wrongHp == 0 && noName == 0 && mages > 0, "random characters: right number of skills, HP = CON, WP = WIL, attributes 1..18, some mages");
    }

    // ------------------------------------------------------------------------------------- files
    Character round = c;
    round.id = "c-test-1";
    round.player = "Sebastián";
    round.conditions = 0b100001;
    round.notes = "line one\nline \"two\" with ü and \\ backslash";
    round.tinyItems = {"a bone whistle"};
    round.memento = "A bone whistle";
    Character back;
    check(Character::fromJson(round.toJson(), back, nullptr), "character JSON parses");
    check(back.name == round.name && back.player == round.player && back.age == round.age && back.kin.key == round.kin.key &&
              std::equal(back.attr, back.attr + 6, round.attr) && back.skills.size() == round.skills.size() && back.conditions == round.conditions &&
              back.notes == round.notes && back.weapons.size() == round.weapons.size() && back.armor.name == round.armor.name &&
              back.abilities.size() == round.abilities.size() && back.tinyItems == round.tinyItems &&
              back.silver == round.silver && back.inventory.size() == round.inventory.size(),
          "a character survives JSON round-trip (accents, quotes, newlines)");
    check(back.skills[0].ref.key == round.skills[0].ref.key && back.skills[0].level == round.skills[0].level && back.skills[0].trained,
          "skills keep key, level and trained flag");
    Character lenient;
    check(Character::fromJson("{\"name\":\"Min\",\"attributes\":{\"STR\":\"15\"},\"age\":\"ancient\",\"skills\":[{\"name\":\"Axes\"}]}", lenient, nullptr) &&
              lenient.attr[0] == 15 && lenient.age == "adult" && lenient.skills.size() == 1 && lenient.hp == 10,
          "a hand-written minimal character is accepted");
    check(!Character::fromJson("[1,2]", lenient, &err) && !Character::fromJson("{ broken", lenient, &err), "garbage is refused");
    const std::string text = c.summary();
    check(text.find("Brenna \"Grimjaw\"") == 0 && text.contains("Human Fighter, Adult") && text.contains("Axes 12") &&
              text.contains("Veteran") && text.contains("Morningstar") && text.contains("HP 12/12"),
          "the plain-text summary lists identity, stats, skills, abilities and gear");

    const std::string dir = test::scratch("characters");
    for (const char* f : {"a.json", "b.json"}) SDL_RemovePath((dir + "/" + f).c_str());
    CharacterStore store;
    store.setDir(dir);
    for (const Character& old : std::vector<Character>(store.all())) store.remove(old.id);
    check(store.all().empty(), "an empty store");
    Character one = c;
    one.id.clear();
    check(store.save(one, &err) && !one.id.empty() && one.createdAt.size() == 20 && one.updatedAt.size() == 20, "save gives an id and timestamps");
    Character two = yc;
    two.name = "Albin";
    two.id.clear();
    store.save(two, &err);
    check(store.all().size() == 2 && store.all()[0].name == "Albin", "the list is kept sorted by name");
    const int rev = store.revision();
    one.hp = 3;
    store.save(one, &err);
    check(store.all().size() == 2 && store.find(one.id)->hp == 3 && store.revision() > rev, "saving again replaces, not duplicates");
    CharacterStore other;
    other.setDir(dir);
    check(other.all().size() == 2 && other.find(one.id) && other.find(one.id)->hp == 3, "a new store reads the files from disk");
    const std::string exported = dir + "/exported.txt";
    check(store.exportFile(one, exported, &err), "export to a chosen file");
    std::string imported;
    check(store.importFile(exported, &imported, &err) && imported != one.id && store.all().size() == 3, "import always creates a new character");
    check(!store.importFile(dir + "/nothing.json", &imported, &err), "importing a missing file fails cleanly");
    test::write(dir + "/../evil.json", "{}");
    check(!store.remove("../evil") && !store.remove("nothere") && test::exists(dir + "/../evil.json"), "removing checks the id: no path tricks");
    SDL_RemovePath((dir + "/../evil.json").c_str());
    check(store.remove(one.id) && !test::exists(dir + "/" + one.id + ".json") && store.all().size() == 2, "remove deletes the file");
    test::write(dir + "/junk.json", "not json at all");
    store.reload();
    check(store.all().size() == 2, "a damaged file is skipped, the rest loads");
    for (const Character& ch : std::vector<Character>(store.all())) store.remove(ch.id);
    SDL_RemovePath((dir + "/junk.json").c_str());
    SDL_RemovePath(exported.c_str());

    // ---------------------------------------------------------------------------------------- parties
    {
        const std::string pdir = test::scratch("parties");
        PartyStore parties;
        parties.setDir(pdir);
        for (const Party& old : std::vector<Party>(parties.all())) parties.remove(old.id);
        Party party;
        party.name = "Zeta party";
        party.members = {"c-1", "c-2"};
        party.notes = "line one\nwith \"quotes\" and ü";
        check(parties.save(party, &err) && !party.id.empty() && party.id[0] == 'p' && party.createdAt.size() == 20, "a party is saved with an id and timestamps");
        Party second;
        second.name = "Alpha party";
        parties.save(second);
        check(parties.all().size() == 2 && parties.all()[0].name == "Alpha party", "parties are kept sorted by name");
        Party back2;
        check(Party::fromJson(party.toJson(), back2, nullptr) && back2.members == party.members && back2.notes == party.notes && back2.name == party.name,
              "a party survives a JSON round-trip");
        Party dupes;
        check(Party::fromJson("{\"name\":\"D\",\"members\":[\"a\",\"b\",\"a\"]}", dupes, nullptr) && dupes.members.size() == 2, "a character listed twice counts once");
        check(!Party::fromJson("[1]", dupes, &err) && !Party::fromJson("{ nope", dupes, &err), "garbage is refused");

        check(parties.addMember(party.id, "c-3") && parties.find(party.id)->members.size() == 3 && !parties.addMember(party.id, "c-3") &&
                  !parties.addMember("p-nope", "c-9") && !parties.addMember(party.id, ""),
              "adding a member: once, to an existing party, not empty");
        check(parties.partyOf("c-2") && parties.partyOf("c-2")->id == party.id && !parties.partyOf("c-404"), "which party a character is in");
        parties.forgetCharacter("c-2");
        check(!parties.find(party.id)->has("c-2") && parties.find(party.id)->members.size() == 2, "a deleted character leaves every party");
        PartyStore fresh;
        fresh.setDir(pdir);
        check(fresh.all().size() == 2 && fresh.find(party.id) && !fresh.find(party.id)->has("c-2"),
              "a new store reads the parties from disk, changes included");
        test::write(pdir + "/junk.json", "not json");
        test::write(pdir + "/bad id.json", "{\"name\":\"x\"}");
        fresh.reload();
        check(fresh.all().size() == 2, "a damaged file, or one with an unsafe name, is skipped");
        check(!parties.remove("../x") && parties.remove(party.id) && parties.remove(second.id) && parties.all().empty(), "removing checks the id and deletes the file");
        SDL_RemovePath((pdir + "/junk.json").c_str());
        SDL_RemovePath((pdir + "/bad id.json").c_str());
    }

    return test::finish();
}
