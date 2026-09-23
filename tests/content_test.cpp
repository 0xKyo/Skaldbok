// The Core content pack, homebrew packs (loading, validation, importing, homerules) and search.
// Needs data/packs/core (the book pack, kept out of git because the books are copyrighted).
#include <set>
#include <string>
#include <vector>

#include "content.h"
#include "dice.h"
#include "packs.h"
#include "testutil.h"

using namespace gm;
using test::check;

namespace {

const Entry* find(const ContentStore& s, Kind k, const std::string& name) { return s.findByName(k, name); }

const Monster* monsterOf(const ContentStore& s, const std::string& name, const std::string& sourceKey) {
    for (const Monster& m : s.monsters()) {
        const SourceInfo* si = s.source(m.sourceId);
        if (m.name == name && si && si->key == sourceKey) return &m;
    }
    return nullptr;
}

bool hasWarning(const PackInfo& p, const std::string& needle) {
    for (const std::string& w : p.warnings)
        if (w.contains(needle)) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dataDir = test::dataDir(argc, argv);
    const std::string coreDir = dataDir + "/packs/core";
    const std::string example = test::sourceDir() + "/examples/frostmarch-tales";

    // ------------------------------------------------------------------------ the built-in pack: Core, with no manifest.json
    ContentStore core;
    core.load({PackSpec{coreDir, true, true}});
    check(core.packs().size() == 1 && core.packs()[0].id == "core" && core.packs()[0].loaded && core.packs()[0].error.empty(),
          "Core loads: its id is the folder's name, with no manifest.json: " + core.packs()[0].error);
    check(!test::exists(coreDir + "/manifest.json") && core.packs()[0].name == "Dragonbane Core" && core.packs()[0].author == "Free League",
          "Core describes itself in the header of rules.json (name, author...)");
    {
        int books = 0;
        for (const SourceInfo& s : core.sources()) books += s.book;
        check(books == 3, "three book sources");
        const int rulebook = core.bookId("rulebook");
        const SourceInfo* rb = core.source(rulebook);
        check(rb && rb->packId == "core" && rb->file.ends_with("Dragonbane_Rulebook.pdf") && core.pageCount(rulebook) == 132,
              "the books are declared by Core: their PDF and page count");
        check(core.printedPage(rulebook, 9) == 5 && core.printedPage(rulebook, 1) == 0 && core.printedPage(rulebook, 999) == 0,
              "printed page numbers: page 9 is printed 5, a cover page has none");
    }
    check(core.monsters().size() > 150, "more than 150 creatures");
    check(core.count(Kind::Spell) == 66, "66 spells and tricks");
    check(core.count(Kind::Ability) == 51, "44 heroic + 7 kin abilities");
    check(core.count(Kind::Skill) == 34 && core.count(Kind::Kin) == 6 && core.count(Kind::Profession) == 10, "34 skills, 6 kin, 10 professions");
    check(core.count(Kind::Weapon) == 36 && core.count(Kind::Armor) == 6 && core.count(Kind::Gear) == 116, "equipment: 36 weapons, 6 armors, 116 items");

    const Monster* centaur = monsterOf(core, "Centaur", "bestiary");
    check(centaur && centaur->attacks.size() == 6 && centaur->attackDice == "D6", "Centaur (Bestiary) has 6 attacks on a D6");
    check(centaur && !centaur->blocks.empty() && !centaur->blocks[0].fields.empty(), "Centaur has a stat block");
    check(centaur && !centaur->image.empty() && test::exists(centaur->image), "Centaur has art, and the file exists");
    check(centaur && centaur->ref.printed == 18 && centaur->ref.valid(), "Centaur links to printed page 18");
    check(centaur && centaur->attacks[0].rollMin == 1 && centaur->attacks[5].rollMax == 6, "attack rolls cover 1..6");
    const Monster* worg = monsterOf(core, "The Worg Rider", "adventure");
    std::string hp;
    if (worg && !worg->blocks.empty())
        for (const Field& f : worg->blocks[0].fields)
            if (f.label == "HP") hp = f.value;
    check(worg && worg->attacks.size() == 6 && hp == "24", "The Worg Rider (Adventure): 6 attacks, HP exactly 24");
    check(monsterOf(core, "Goblin", "rulebook") && monsterOf(core, "Goblin", "bestiary"), "Goblin exists in the Rulebook and in the Bestiary");
    check(centaur && core.creatureVersions("Goblin", monsterOf(core, "Goblin", "rulebook")->id).size() == 1, "the other version of a creature is found");
    check(centaur && !core.creaturesOnPrintedPage(centaur->sourceId, 18).empty(), "creatures on a printed page are found");

    const Entry* human = find(core, Kind::Kin, "Human");
    check(human && human->prop("movement") == "10" && human->list("names").size() == 6, "Human: movement 10 and six names");
    check(human && human->body.contains("Adaptive"), "the kin card includes its innate ability");
    check(human && human->tables.size() == 1 && human->tables[0].title == "Human: First Name" && human->tables[0].dice == "D6" && human->tables[0].rows.size() == 6 &&
              !human->tables[0].browse && human->tables[0].id == 0 && human->tables[0].rule == 0,
          "a kin card holds its own table of first names (not one of the rules' tables)");
    check(human && human->list("names").size() == 6 && human->list("names")[0] == human->tables[0].rows[0].cells[0] && human->list("names")[5] == human->tables[0].rows[5].cells[0],
          "the list of names the creator uses is the first column of that table: kept once");
    {
        bool found = false;
        for (const Hit& h : core.search("Joruna")) found |= h.kind == Kind::Kin && h.title == "Human";
        check(found, "a name in a kin's table finds the kin's card in the search");
    }
    check(!core.tableByRole("human-first-name") && core.count(Kind::Table) == static_cast<int>(core.packTables().size()), "the kin tables are not among the tables of the rules");
    const Entry* mage = find(core, Kind::Profession, "Mage");
    check(mage && mage->list("schools").size() == 3 && mage->prop("magic") == "1" && mage->list("skills.Animism").size() == 8,
          "Mage: three schools, 8 skills each, starts with magic");
    const Entry* fighter = find(core, Kind::Profession, "Fighter");
    check(fighter && fighter->list("skills").size() == 8 && fighter->list("starting_gear").size() == 3 &&
              fighter->list("heroic_abilities") == std::vector<std::string>{"Veteran"},
          "Fighter: 8 skills, 3 gear sets, heroic ability Veteran");
    check(fighter && fighter->tables.size() == 2 && fighter->tables[0].title == "Fighter: Gear" && fighter->tables[0].dieSides() == 6 &&
              fighter->tables[0].rows.size() == 3 && fighter->tables[1].title == "Fighter: Nickname" && fighter->tables[1].rows.size() == 6 &&
              fighter->list("starting_gear")[0] == fighter->tables[0].rows[0].cells[0] && fighter->list("nicknames")[0] == fighter->tables[1].rows[0].cells[0],
          "a profession card holds its own Gear and Nickname tables; the lists the creator uses come from them");
    const Entry* axes = find(core, Kind::Skill, "Axes");
    check(axes && axes->prop("attribute") == "STR" && find(core, Kind::Skill, "Bows")->prop("attribute") == "AGL", "weapon skills know their attribute");
    const Entry* fetch = find(core, Kind::Spell, "Fetch");
    check(fetch && fetch->prop("trick") == "1" && fetch->prop("school") == "General Magic", "Fetch is a General Magic trick");

    // keys survive: a saved key finds the same entry again
    check(human && core.idByKey(Kind::Kin, human->key) == human->id && human->key == "core/kin/human", "stable keys: core/kin/human");
    check(centaur && core.idByKey(Kind::Monster, core.keyOf(Kind::Monster, centaur->id)) == centaur->id, "creature key round-trips");
    {
        const int table = core.idByKey(Kind::Table, "#31");                  // saved boards and recents from before the move: "#31" (Movement)
        check(table >= kPackTableBase && core.keyOf(Kind::Table, table).starts_with("core/table/"), "old numeric table keys resolve to the table's new key");
    }

    // tables the character creator rolls on
    const DataTable* weakness = core.tableByRole("weakness");
    check(weakness && weakness->dieSides() == 20 && weakness->rows.size() == 20 && !weakness->browse, "Weakness role table: D20, 20 rows, not listed twice");
    bool everyRoll = weakness != nullptr;
    for (int r = 1; weakness && r <= 20; ++r) everyRoll &= weakness->rowForRoll(r) >= 0;
    check(everyRoll, "every D20 roll maps to a row");
    check(core.tableByRole("memento") && core.tableByRole("appearance"), "memento and appearance tables exist");
    {   // the books' own tables live in Core, inside their sections, and keep the numeric keys they had ("#12") as aliases
        int bookTables = 0, rolled = 0;
        const DataTable* bookWeakness = nullptr;
        for (const DataTable& t : core.packTables())
            if (t.key.starts_with("core/table/")) {
                ++bookTables;
                if (t.title == "Weakness" && t.browse) bookWeakness = &t;
            }
        check(bookTables >= 40, "the books' tables not on a card of their own are in Core: " + std::to_string(bookTables));
        check(bookWeakness && bookWeakness->rows.size() == 20 && bookWeakness->dieSides() == 20, "the book's Weakness table: D20, 20 rows");
        check(bookWeakness && core.idByKey(Kind::Table, "#" + std::to_string(1)) != 0, "an old numeric table key still finds a table");
        for (const DataTable& t : core.packTables()) rolled += t.dieSides() > 0;
        check(rolled > 30, "dice tables keep their dice");
    }

    // search: everything a pack provides, the books' tables included
    auto hits = core.search("centaur attacks");
    bool centaurHit = false;
    for (const Hit& h : hits) centaurHit |= h.kind == Kind::Monster && h.title == "Centaur";
    check(centaurHit, "search 'centaur attacks' finds the Centaur creature");
    check(!core.search("dragonslayer").empty(), "prefix search 'dragonslayer'");
    hits = core.search("fetch");
    check(!hits.empty() && hits[0].exact && hits[0].title == "Fetch", "an exact title comes first");
    check(core.search("").empty() && core.search("   ").empty(), "empty search finds nothing");
    check(!core.search("Measuring Time").empty() && core.search("Measuring Time")[0].kind == Kind::Table, "a table of the books is found by search");

    // Rules: book text minus what has its own category (creatures, spells, skills, kin...), the preface and the indexes; the adventure has its own section.
    {
        const std::vector<RuleNode>& rules = core.rules();
        std::set<std::string> titles;
        std::set<int> parents;
        for (const RuleNode& n : rules) parents.insert(n.parent);
        for (const RuleNode& n : rules) {
            titles.insert(n.title);
            if (n.body.empty() && !parents.count(n.id) && core.tablesOfRule(n.id).empty()) check(false, "a rule heading without text, children or tables was kept");
            if (n.sourceId == core.bookId("adventure")) {                    // the adventure lives under "Adventures", and nowhere else
                int up = n.id;
                while (core.rule(up) && core.rule(up)->parent) up = core.rule(up)->parent;
                if (!core.rule(up) || core.rule(up)->key != "core/rule/adventure-library") check(false, "an adventure rule is outside the Adventures section: " + n.title);
            }
            if (n.parent && !core.rule(n.parent)) check(false, "a rule points at a parent that is not there");
        }
        check(rules.size() > 100 && rules.size() < 1500, "a review-sized set of rules text: " + std::to_string(rules.size()));
        {   // the adventure, all of it, under Adventures > The Misty Vale; its tables inside the places they belong to
            const int library = core.ruleByKey("core/rule/adventure-library"), vale = core.ruleByKey("core/rule/the-misty-vale");
            int inVale = 0;
            for (const RuleNode& n : rules) inVale += n.sourceId == core.bookId("adventure");
            check(library && vale && core.rule(vale)->parent == library && core.rule(library)->parent == 0 && core.rule(library)->title == "Adventures",
                  "the adventure has its own section: Adventures > The Misty Vale");
            check(inVale > 100 && parents.count(vale), "the adventure's text is in it, many sections deep: " + std::to_string(inVale));
            const DataTable* magna = nullptr;
            for (const DataTable& t : core.packTables())
                if (t.title == "Random Encounters in the Magna Woods") magna = &t;
            check(magna && core.rule(magna->rule) && core.rule(magna->rule)->title == "Journeys" && magna->rows.size() == 7, "an adventure table is inside its chapter (Journeys)");
            const DataTable* time = nullptr;
            for (const DataTable& t : core.packTables())
                if (t.title == "Measuring Time") time = &t;
            check(time && core.rule(time->rule) && core.rule(time->rule)->id == 1, "Measuring Time is at the top of the rules, in the first rule");
        }
        {   // Character Creation: kin and innate ability are one step (they are the same topic); eleven more follow, in order,
            // each pointing to where its choices come from
            const int cc = core.ruleByKey("core/rule/character-creation");
            std::vector<const RuleNode*> steps;
            for (const RuleNode& n : rules)
                if (n.parent == cc && !n.prop("step").empty()) steps.push_back(&n);
            bool inOrder = steps.size() == 12;
            for (size_t i = 0; i < steps.size(); ++i) inOrder &= steps[i]->prop("step") == std::to_string(i + 1);
            check(cc && core.rule(cc)->title == "Character Creation" && core.rule(cc)->parent == 0 && inOrder, "Character Creation has the book's twelve steps, numbered in order");
            auto step = [&](const char* key) { return core.rule(core.ruleByKey(std::string("core/rule/") + key)); };
            const auto& kinSee = step("creation-kin")->see;
            check(step("creation-kin") && step("creation-kin")->prop("step") == "1" && step("creation-kin")->body.contains("innate ability") &&
                      std::find(kinSee.begin(), kinSee.end(), "kin") != kinSee.end() && std::find(kinSee.begin(), kinSee.end(), "abilities") != kinSee.end() &&
                      !core.ruleByKey("core/rule/creation-innate-ability"),
                  "kin and its innate ability are one step, with both their \"see\" links");
            check(step("creation-profession") && step("creation-profession")->prop("step") == "2" && step("creation-age") && step("creation-age")->prop("step") == "3" &&
                      step("creation-name") && step("creation-name")->prop("step") == "4" && step("creation-attributes") && step("creation-attributes")->prop("step") == "5" &&
                      step("creation-derived-ratings") && step("creation-derived-ratings")->prop("step") == "6" && step("creation-skills") &&
                      step("creation-skills")->prop("step") == "7" && step("creation-heroic-ability") && step("creation-heroic-ability")->prop("step") == "8" &&
                      step("creation-weakness") && step("creation-weakness")->prop("step") == "9" && step("creation-gear") && step("creation-gear")->prop("step") == "10" &&
                      step("creation-memento") && step("creation-memento")->prop("step") == "11" && step("creation-appearance") && step("creation-appearance")->prop("step") == "12",
                  "each step has a stable key (core/rule/creation-...) and its new number");
            check(step("creation-profession")->body.contains("(step 7)") && step("creation-profession")->body.contains("(step 10)") &&
                      step("creation-profession")->body.contains("(step 8, mages") && step("creation-profession")->body.contains("(step 4)") &&
                      step("creation-attributes")->body.contains("(step 3)") && step("creation-skills")->body.contains("(step 10)") &&
                      !step("creation-profession")->body.contains("(step 11)") && !step("creation-profession")->body.contains("(step 9,") &&
                      !step("creation-profession")->body.contains("(step 5)"),
                  "a \"(step N)\" mention inside a step's own text was renumbered along with the steps, not chained into some other step's number");
            check(!core.ruleByKey("core/rule/creation-language") && step("creation-kin")->body.contains("common tongue") &&
                      std::find(kinSee.begin(), kinSee.end(), "core/skill/languages") != kinSee.end(),
                  "Language is not a rule of its own any more: its text and \"see\" are part of Kin");
            // eight steps that used to have clickable children are now one page each: a table (or props, for Age) and/or named sections
            const RuleNode* age = step("creation-age");
            check(age && age->prop("young_trained_skills") == "6" && age->prop("young_bonus_skills") == "2" && age->prop("young_attribute_mods") == "{\"AGL\":1,\"CON\":1}" &&
                      age->prop("old_attribute_mods") == "{\"STR\":-2,\"AGL\":-2,\"CON\":-2,\"INT\":1,\"WIL\":1}" && core.tablesOfRule(age->id).size() == 1 &&
                      core.packTable(core.tablesOfRule(age->id)[0])->title == "Effects of Age" && age->sections.empty(),
                  "Age has no children: a table like the book's, the numbers kept as data on the rule itself");
            auto sectionNames = [](const RuleNode& n) {
                std::vector<std::string> names;
                for (const RuleNode::Section& s : n.sections) names.push_back(s.title);
                return names;
            };
            check(sectionNames(*step("creation-attributes")) == std::vector<std::string>{"Starting Scores", "Other Methods"} &&
                      core.tablesOfRule(step("creation-attributes")->id).size() == 1 &&
                      core.packTable(core.tablesOfRule(step("creation-attributes")->id)[0])->rows.size() == 6,
                  "Attributes: one page, with a table of the six attributes and two sections");
            check(sectionNames(*step("creation-derived-ratings")) == (std::vector<std::string>{"Hit Points (HP)", "Willpower Points (WP)"}) &&
                      core.tablesOfRule(step("creation-derived-ratings")->id).size() == 3,
                  "Derived Ratings: one page, with Movement's two tables and Damage Bonus's promoted up to it");
            check((sectionNames(*step("creation-skills")) == std::vector<std::string>{"Base Chance", "Starting Skill Levels", "Secondary Skills", "Magic"}) &&
                      core.tablesOfRule(step("creation-skills")->id).size() == 1,
                  "Trained Skills: one page, Base Chance's table promoted up, four sections");
            check(sectionNames(*step("creation-heroic-ability")) == std::vector<std::string>{"Mages", "Alternative Abilities"},
                  "Heroic Ability: the Mages exception is its own clear section, split from Alternative Abilities");
            check(sectionNames(*step("creation-gear")) == std::vector<std::string>{"Starting Gear", "Coins"}, "Gear: one page, two sections");
            const RuleNode* enc = nullptr;
            const RuleNode* exp = nullptr;
            for (const RuleNode& n : rules) {
                if (n.title == "Encumbrance" && n.parent == cc) enc = &n;
                if (n.title == "Experience" && n.parent == cc) exp = &n;
            }
            check(enc && sectionNames(*enc).size() == 10 && sectionNames(*enc)[0] == "Weapons at Hand" && sectionNames(*enc).back() == "Riding Animals & Vehicles",
                  "Encumbrance: one page, its ten topics as sections (the deeply nested tree flattened)");
            check(exp && sectionNames(*exp) == (std::vector<std::string>{"Advancement Marks", "Advancement Rolls", "Teacher", "Magic", "Heroic Abilities", "Overcome Weakness"}),
                  "Experience: one page too");
            for (const char* id :
                 {"creation-age", "creation-attributes", "creation-derived-ratings", "creation-skills", "creation-heroic-ability", "creation-gear", "creation-profession"}) {
                bool anyChild = false;
                for (const RuleNode& n : rules) anyChild |= n.parent == step(id)->id;
                check(!anyChild, std::string(id) + " has no clickable children left");
            }
            int unresolved = 0, links = 0;
            for (const RuleNode& n : rules)
                for (const std::string& ref : n.see) {
                    ++links;
                    unresolved += core.seeTarget(ref).type == SeeTarget::Type::None;
                }
            check(links > 20 && unresolved == 0, "every \"see\" of Core points to something that is there (" + std::to_string(links) + " links)");
            check(core.packs()[0].warnings.empty(), "Core loads with no warnings");
        }
        check(titles.count("Melee Combat"), "general rules are kept");
        check(!titles.count("Contents") && !titles.count("Index") && !titles.count("Preface"), "contents, index and preface are dropped");
        check(!titles.count("Dragon") && !titles.count("Fireball"), "creatures and spells are not repeated");
    }

    // ------------------------------------------------------------------------------------ homebrew
    {
        ContentStore s;
        s.load({PackSpec{coreDir, true, true}, PackSpec{example, false, true}});
        const PackInfo* hb = s.pack("frostmarch");
        check(hb && hb->loaded && hb->error.empty() && hb->warnings.empty(), "example pack loads with no warnings");
        check(hb && hb->counts[static_cast<int>(Kind::Spell)] == 3 && hb->counts[static_cast<int>(Kind::Monster)] == 1 &&
                  hb->counts[static_cast<int>(Kind::Kin)] == 1 && hb->counts[static_cast<int>(Kind::Table)] == 1,
              "example pack counts: 3 spells, 1 creature, 1 kin, 1 table");
        check(s.count(Kind::Spell) == 69 && core.count(Kind::Spell) == 66, "homebrew spells are added to Core's, not replacing them");
        const Entry* frost = find(s, Kind::Kin, "Frostborn");
        const SourceInfo* src = frost ? s.source(frost->sourceId) : nullptr;
        check(src && src->homebrew && src->label == "Homebrew · Frostmarch" && src->packId == "frostmarch", "the source is kept: \"Homebrew · Frostmarch\"");
        check(frost && frost->key == "frostmarch/kin/frostborn" && frost->prop("movement") == "9", "homebrew keys are namespaced by pack");
        check(frost && frost->body.contains("Winter's Hide") && frost->body.contains("long winter"),
              "paragraph lists are joined, innate ability text is appended");
        check(frost && !frost->ref.valid() && frost->pageNote == "p.12", "homebrew has no PDF link, only a page note");
        const Entry* skald = find(s, Kind::Profession, "Skald");
        check(skald && skald->list("heroic_abilities") == std::vector<std::string>{"Icebreaker", "Musician"} && skald->list("skills").size() == 8,
              "profession lists a heroic ability of the pack and one of Core");
        const Monster* wight = monsterOf(s, "Frost Wight", "frostmarch");
        check(wight && test::exists(wight->image) && wight->attacks.size() == 4 && wight->attacks[0].rollMin == 1 && wight->attacks[0].rollMax == 2,
              "creature: art found, attack rolls '1-2' parsed");
        check(wight && wight->attacks[0].text.starts_with("Rimed Claws") && wight->blocks.size() == 1 && wight->blocks[0].fields.size() == 5 &&
                  wight->blocks[0].fields[0].label == "Ferocity" && wight->blocks[0].fields[4].label == "HP",
              "creature: attack names kept, stat block fields keep the author's order");
        const DataTable* weather = nullptr;
        for (const DataTable& t : s.packTables())
            if (t.title == "Frostmarch Weather") weather = &t;
        check(weather && weather->id >= kPackTableBase && weather->rowForRoll(4) == 2 && s.packTable(weather->id) == weather, "pack table: ids above the book range, '3-4' covers a roll of 4");
        hits = s.search("frost wight");
        check(!hits.empty() && hits[0].kind == Kind::Monster && hits[0].title == "Frost Wight", "homebrew is found by search");
        check(!s.search("rime ward").empty(), "homebrew spell is found by search");
        check(!s.search("Frostmarch Weather").empty() && s.search("Frostmarch Weather")[0].kind == Kind::Table, "homebrew table is found by search");
        check(s.count(Kind::Monster) == core.count(Kind::Monster) + 1, "homebrew creature comes on top of Core's 189");
        check(s.entry(Kind::Kin, s.idByKey(Kind::Kin, "frostmarch/kin/frostborn")) == frost, "homebrew key resolves back to the entry");
        check(s.idByKey(Kind::Kin, "gone/kin/nothing") == 0, "an unknown key resolves to nothing");
    }

    // disabled packs are listed but not loaded
    {
        ContentStore s;
        s.load({PackSpec{coreDir, true, true}, PackSpec{example, false, false}});
        const PackInfo* hb = s.pack("frostmarch");
        check(hb && !hb->enabled && !hb->loaded && hb->total() == 0 && s.count(Kind::Spell) == 66, "a disabled pack is listed, nothing of it is loaded");
    }

    // ------------------------------------------------------------------------------- homerules: rules of other packs
    {
        const std::string root = test::scratch("homerules");
        removeTree(root);
        const int before = static_cast<int>(core.rules().size());
        const int combat = core.ruleByKey("core/rule/combat-damage");
        check(combat != 0 && core.rule(combat) && core.rule(combat)->parent == 0, "a rule of Core is found by its key");
        const std::string melee = "core/rule/melee-combat";
        const int meleeId = core.ruleByKey(melee);
        check(meleeId != 0 && core.rule(meleeId)->parent == combat && core.rule(meleeId)->level == 2, "and a child knows its parent and level");

        test::write(root + "/house/manifest.json", "{\"format\":1,\"id\":\"house\",\"name\":\"House Rules\"}");
        test::write(root + "/house/rules.json",
                    "{\"rules\":["
                    "{\"id\":\"crits\",\"name\":\"Critical Failures\",\"body\":[\"A 20 on a skill roll is a disaster.\",\"The GM decides how.\"],\"parent\":\"core/rule/combat-damage\"},"
                    "{\"id\":\"crit-fumble\",\"name\":\"Fumble table\",\"body\":\"Roll on it.\",\"parent\":\"crits\"},"
                    "{\"name\":\"Melee, our way\",\"body\":\"No free parries.\",\"replaces\":\"core/rule/melee-combat\"},"
                    "{\"name\":\"Lost\",\"body\":\"x\",\"parent\":\"core/rule/not-there\"},"
                    "{\"name\":\"Ghost\",\"body\":\"x\",\"replaces\":\"core/rule/not-there\"}]}");
        ContentStore s;
        s.load({PackSpec{coreDir, true, true}, PackSpec{root + "/house", false, true}});
        const PackInfo* house = s.pack("house");
        check(house && house->loaded && house->rules == 4 && house->warnings.size() == 2, "a pack of homerules loads; the broken references are reported");
        check(house && hasWarning(*house, "not-there"), "and the warnings name the missing rule");
        const int crits = s.ruleByKey("house/rule/crits");
        const RuleNode* c = s.rule(crits);
        check(c && c->parent == s.ruleByKey("core/rule/combat-damage") && c->level == 2 && c->body.contains("disaster") && c->body.contains("The GM decides"),
              "a homerule goes under a rule of the base pack; paragraph lists are joined");
        const RuleNode* fumble = s.rule(s.ruleByKey("house/rule/crit-fumble"));
        check(fumble && fumble->parent == crits && fumble->level == 3, "and a homerule can go under another one of its own pack");
        const RuleNode* lost = s.rule(s.ruleByKey("house/rule/lost"));
        check(lost && lost->parent == 0 && lost->level == 1, "a homerule whose parent is missing goes to the top");
        const RuleNode* melee2 = s.rule(s.ruleByKey(melee));
        check(melee2 && melee2->title == "Melee, our way" && melee2->body == "No free parries." && melee2->editedBy == "House Rules" &&
                  melee2->parent == combat && s.ruleByKey(melee) == meleeId,
              "a replaced rule keeps its place and key, and says who changed it");
        check(s.rules().size() == static_cast<size_t>(before) + 3, "replacing adds no rule; the other three are new");
        const SourceInfo* hs = melee2 ? s.source(melee2->sourceId) : nullptr;
        check(hs && hs->homebrew && hs->packId == "house", "the replaced rule now shows the homebrew source");
        check(core.rule(meleeId)->editedBy.empty() && core.rule(meleeId)->title != "Melee, our way", "the stores are independent: the base is as it was");

        // rules written as a tree: children inside their parent, ids from the names, the source inherited
        test::write(root + "/tree/manifest.json",
                    "{\"format\":1,\"id\":\"tree\",\"name\":\"Tree Rules\",\"sources\":[{\"key\":\"zine\",\"title\":\"The Zine\"},{\"key\":\"blog\",\"title\":\"The Blog\"}]}");
        test::write(root + "/tree/rules.json",
                    "{\"rules\":["
                    "{\"name\":\"Camping\",\"body\":\"Rest well.\",\"source\":\"zine\",\"page\":4,\"children\":["
                    "  {\"name\":\"Fires\",\"body\":\"Keep one.\",\"children\":[{\"name\":\"Kindling\",\"body\":\"Dry wood only.\"}]},"
                    "  {\"name\":\"Watches\",\"source\":\"blog\"},"
                    "  {\"body\":\"no name here\"},"
                    "  {\"id\":\"night\",\"name\":\"Night Shift\",\"parent\":\"core/rule/combat-damage\",\"body\":\"the parent is the tree, not this\"}]},"
                    "{\"name\":\"Camping\",\"body\":\"a second one\"},"
                    "{\"name\":\"Melee, tree style\",\"replaces\":\"core/rule/melee-combat\",\"body\":\"New text.\",\"children\":[{\"name\":\"Grapples\",\"body\":\"Hold on.\"}]}"
                    "]}");
        ContentStore u;
        u.load({PackSpec{coreDir, true, true}, PackSpec{root + "/tree", false, true}});
        const PackInfo* tree = u.pack("tree");
        check(tree && tree->loaded && tree->rules == 8 && tree->warnings.size() == 1 && hasWarning(*tree, "has no \"name\""),
              "a tree of rules loads; a child without a name is skipped with a warning");
        const int camping = u.ruleByKey("tree/rule/camping");
        const RuleNode* camp = u.rule(camping);
        const int fires = u.ruleByKey("tree/rule/fires"), kindling = u.ruleByKey("tree/rule/kindling");
        check(camp && camp->parent == 0 && camp->level == 1 && u.rule(fires) && u.rule(fires)->parent == camping && u.rule(fires)->level == 2 &&
                  u.rule(kindling) && u.rule(kindling)->parent == fires && u.rule(kindling)->level == 3,
              "children go under the rule they are written in, as deep as needed");
        const SourceInfo* zine = camp ? u.source(camp->sourceId) : nullptr;
        check(zine && zine->key == "zine" && u.rule(kindling) && u.rule(kindling)->sourceId == camp->sourceId && u.rule(fires)->sourceId == camp->sourceId,
              "a child without a source has its parent's, all the way down");
        const RuleNode* watches = u.rule(u.ruleByKey("tree/rule/watches"));
        const SourceInfo* blog = watches ? u.source(watches->sourceId) : nullptr;
        check(blog && blog->key == "blog", "and a child can name another source of the pack");
        check(camp && camp->ref.valid() == false && camp->pageNote == "p.4", "a pack without a PDF gets a page note");
        const RuleNode* night = u.rule(u.ruleByKey("tree/rule/night"));
        check(night && night->parent == camping, "inside a tree, 'parent' is ignored: the place it is written wins");
        check(u.ruleByKey("tree/rule/camping-2") != 0 && u.rule(u.ruleByKey("tree/rule/camping-2"))->parent == 0, "two rules with the same name get different keys");
        const RuleNode* melee3 = u.rule(u.ruleByKey(melee));
        const RuleNode* grapples = u.rule(u.ruleByKey("tree/rule/grapples"));
        check(melee3 && melee3->body == "New text." && melee3->editedBy == "Tree Rules" && grapples && grapples->parent == meleeId && grapples->level == 3,
              "a replacing rule can bring children, which go below the replaced one");

        // tables written inside a rule; the ones of tables.json get a rule of their own
        test::write(root + "/tabs/manifest.json", "{\"format\":1,\"id\":\"tabs\",\"name\":\"Tab Pack\",\"sources\":[{\"key\":\"zine\",\"title\":\"The Zine\"}]}");
        test::write(root + "/tabs/rules.json",
                    "{\"rules\":[{\"name\":\"Foraging\",\"source\":\"zine\",\"body\":\"Find food.\",\"tables\":["
                    "{\"name\":\"Finds\",\"dice\":\"D6\",\"columns\":[\"FIND\"],\"rows\":[{\"roll\":\"1-3\",\"cells\":[\"Berries\"]},{\"roll\":\"4-6\",\"cells\":[\"Roots\"]}]},"
                    "{\"body\":\"no name\"}],"
                    "\"children\":[{\"name\":\"Fishing\",\"tables\":[{\"name\":\"Catch\",\"columns\":[\"FISH\",\"SIZE\"],\"rows\":[[\"Trout\",\"small\"]]}]}]}]}");
        test::write(root + "/tabs/tables.json",
                    "{\"tables\":[{\"name\":\"Loose Weather\",\"dice\":\"D4\",\"rows\":[\"Rain\",\"Sun\",\"Wind\",\"Fog\"]},"
                    "{\"name\":\"Hidden Roll\",\"browse\":false,\"role\":\"secret\",\"rows\":[[\"x\"]]}]}");
        ContentStore v;
        v.load({PackSpec{coreDir, true, true}, PackSpec{root + "/tabs", false, true}});
        const PackInfo* tabs = v.pack("tabs");
        check(tabs && tabs->loaded && tabs->counts[static_cast<int>(Kind::Table)] == 4 && tabs->warnings.size() == 1 && hasWarning(*tabs, "has no \"name\""),
              "tables inside rules load with the others; a nameless one is skipped with a warning");
        auto tableNamed = [&](const std::string& name) -> const DataTable* {
            for (const DataTable& t : v.packTables())
                if (t.title == name && t.key.starts_with("tabs/")) return &t;
            return nullptr;
        };
        const DataTable* finds = tableNamed("Finds");
        const int foraging = v.ruleByKey("tabs/rule/foraging");
        check(finds && foraging && finds->rule == foraging && v.tablesOfRule(foraging) == std::vector<int>{finds->id} && finds->dieSides() == 6 && finds->rowForRoll(5) == 1,
              "a table written in a rule belongs to it, dice and rows intact");
        const DataTable* catches = tableNamed("Catch");
        const int fishing = v.ruleByKey("tabs/rule/fishing");
        check(catches && fishing && catches->rule == fishing && catches->sourceId == v.rule(foraging)->sourceId && v.source(catches->sourceId)->key == "zine",
              "a table inside a child rule goes to the child, and takes the rule's source");
        const DataTable* loose = tableNamed("Loose Weather");
        const RuleNode* looseHome = loose ? v.rule(loose->rule) : nullptr;
        check(looseHome && looseHome->key == "tabs/rule/other-tables" && looseHome->title == "Tables · Tab Pack" && looseHome->parent == 0 && v.tablesOfRule(looseHome->id).size() == 1,
              "a table of tables.json is listed under a rule of its own: \"Tables · <pack>\"");
        const DataTable* hidden = tableNamed("Hidden Roll");
        check(hidden && hidden->rule == 0 && v.tableByRole("secret") == hidden, "a table that is not browsable is in no rule, and still serves its role");
        int homeless = 0;
        for (const DataTable& t : core.packTables()) {
            const RuleNode* home = core.rule(t.rule);
            bool listed = false;
            if (home)
                for (int id : core.tablesOfRule(home->id)) listed |= id == t.id;
            homeless += t.browse && !listed;
        }
        check(homeless == 0, "every browsable table of the books is shown in some rule");
        const Monster* goblin = monsterOf(core, "Goblin", "bestiary");
        const DataTable* goblinNames = goblin && !goblin->tables.empty() ? core.packTable(goblin->tables[0].id) : nullptr;
        check(goblinNames && goblinNames->title == "Goblin: First Name", "a creature's related table is its own (Goblin: First Name), not the first table with that name");

        // "see" points to other data; every other key of a rule is data for the program
        test::write(root + "/see/manifest.json", "{\"format\":1,\"id\":\"see\",\"name\":\"See Pack\"}");
        test::write(root + "/see/rules.json",
                    "{\"rules\":[{\"name\":\"Pick\",\"step\":\"3\",\"trained_skills\":6,\"optional\":true,\"attribute_mods\":{\"AGL\":1},"
                    "\"see\":[\"kin\",\"professions\",\"core/kin/human\",\"core/rule/melee-combat\",\"nowhere\",\"core/kin/nobody\",\"core/skill/languages\"],"
                    "\"body\":\"x\"},{\"name\":\"One\",\"see\":\"spells\",\"body\":\"y\"}]}");
        ContentStore w;
        w.load({PackSpec{coreDir, true, true}, PackSpec{root + "/see", false, true}});
        const RuleNode* pick = w.rule(w.ruleByKey("see/rule/pick"));
        check(pick && pick->prop("step") == "3" && pick->prop("trained_skills") == "6" && pick->prop("optional") == "true" && pick->prop("attribute_mods") == "{\"AGL\":1}" &&
                  pick->prop("body").empty() && pick->prop("name").empty(),
              "the other keys of a rule are kept as data: text and numbers as written, objects as their JSON, the known keys not among them");
        check(pick && pick->see.size() == 7 && w.rule(w.ruleByKey("see/rule/one"))->see == std::vector<std::string>{"spells"}, "a rule keeps its \"see\" (a list, or a single text)");
        const SeeTarget cat = w.seeTarget("kin"), ent = w.seeTarget("core/kin/human"), rul = w.seeTarget("core/rule/melee-combat"), none = w.seeTarget("nowhere");
        check(cat.type == SeeTarget::Type::Category && cat.kind == Kind::Kin && cat.label == "Kin (6)", "a category names a kind of entries, with how many there are");
        check(ent.type == SeeTarget::Type::Entry && ent.kind == Kind::Kin && ent.label == "Human" && ent.id == w.idByKey(Kind::Kin, "core/kin/human"), "an entry key finds the entry");
        check(rul.type == SeeTarget::Type::Rule && rul.id == meleeId && rul.label == "Melee Combat", "a rule key finds the rule");
        check(none.type == SeeTarget::Type::None && w.seeTarget("core/kin/nobody").type == SeeTarget::Type::None && w.seeTarget("").type == SeeTarget::Type::None, "what is not there points to nothing");
        const PackInfo* seePack = w.pack("see");
        check(seePack && seePack->loaded && seePack->warnings.size() == 2 && hasWarning(*seePack, "nowhere") && hasWarning(*seePack, "core/kin/nobody"), "a \"see\" that points to nothing is a warning");

        // weapons, armor and gear have no mosaic of their own: the book's own tables are gear.json's intro, its own category
        {
            const Intro& gi = core.introOf(Kind::Gear);
            bool termsTable = false, armorTable = false;
            for (const DataTable& t : gi.tables) {
                if (t.title == "Weapons & Armor Terms") termsTable = true;
                if (t.title == "Armor & Helmets") armorTable = true;
            }
            check(!gi.body.empty() && gi.sections.size() == 2 && gi.sections[0].title == "Supply" && gi.sections[1].title == "Weapons & Armor" && gi.tables.size() == 17 &&
                      termsTable && armorTable,
                  "gear.json's intro (moved from the Gear chapter of Rules) has a body, its two sections and all 17 tables");
            check(core.ruleByKey("core/rule/gear-equipment") == 0, "the Gear chapter no longer exists as a rule: it is gear.json's intro now");
            const SeeTarget w1 = core.seeTarget("weapons"), a1 = core.seeTarget("armor"), g1 = core.seeTarget("gear");
            check(w1.type == SeeTarget::Type::Category && w1.kind == Kind::Weapon && a1.type == SeeTarget::Type::Category && a1.kind == Kind::Armor &&
                      g1.type == SeeTarget::Type::Category && g1.kind == Kind::Gear,
                  "a \"see\" of weapons, armor or gear points at that category again (its module is Gear now, not a rule)");
        }

        // the whole Rules > Skills chapter moved into skills.json's intro (see below); nothing is left of it in Rules
        for (const char* key : {"core/rule/skills-2", "core/rule/roll-the-dice", "core/rule/boons-banes", "core/rule/opposed-rolls", "core/rule/the-core-skills"})
            check(core.ruleByKey(key) == 0, (std::string(key) + " no longer exists as a rule: it is part of Skills' intro now").c_str());

        // a data file's own "intro", shown above its mosaic
        check(!core.introOf(Kind::Ability).empty(), "abilities.json's intro (moved from the Skills chapter's \"Heroic Abilities\") is read");
        check(core.introOf(Kind::Kin).empty(), "a kind without an \"intro\" in its data file has none");
        test::write(root + "/intro/manifest.json", "{\"format\":1,\"id\":\"intro\",\"name\":\"Intro\"}");
        test::write(root + "/intro/spells.json", "{\"intro\":\"Homebrew magic works differently here.\",\"spells\":[{\"name\":\"Zap\",\"body\":\"x\"}]}");
        ContentStore ic;
        ic.load({PackSpec{coreDir, true, true}, PackSpec{root + "/intro", false, true}});
        check(ic.introOf(Kind::Spell).body == "Homebrew magic works differently here.", "a pack's \"intro\" for a kind with none yet is picked up");
        check(!core.introOf(Kind::Skill).body.empty() && core.introOf(Kind::Skill).sections.size() >= 10,
              "skills.json's intro (moved from the Skills chapter of Rules) has a body and named sections, like a rule");
        {
            const Intro& spi = core.introOf(Kind::Spell);
            bool hasMishaps = false;
            for (const DataTable& t : spi.tables)
                if (t.title == "Magical Mishaps" && t.dice == "D20") hasMishaps = true;
            check(!spi.body.empty() && spi.sections.size() >= 30 && hasMishaps,
                  "spells.json's intro (moved from the Magic chapter of Rules) has a body, named sections and its Magical Mishaps table");
            for (const char* key : {"core/rule/magic-2", "core/rule/casting-spells", "core/rule/learning-magic", "core/rule/spell-list"})
                check(core.ruleByKey(key) == 0, (std::string(key) + " no longer exists as a rule: it is part of Spells' intro now").c_str());
        }
        {
            const Intro& mi = core.introOf(Kind::Monster);
            bool hasAnimals = false;
            for (const DataTable& t : mi.tables)
                if (t.title == "Common Animals" && t.rows.size() == 11) hasAnimals = true;
            check(!mi.body.empty() && mi.sections.size() >= 15 && hasAnimals,
                  "creatures.json's intro (moved from the Bestiary chapter of Rules) has a body, named sections and its own table");
            for (const char* key : {"core/rule/bestiary", "core/rule/ferocity", "core/rule/monster-attacks", "core/rule/common-animals"})
                check(core.ruleByKey(key) == 0, (std::string(key) + " no longer exists as a rule: it is part of Creatures' intro now").c_str());
            check(core.idByKey(Kind::Table, "core/table/common-animals-rule") == 0 && core.idByKey(Kind::Table, "#71") == 0,
                  "the Common Animals table is a card table of the intro now, not a browsable rule table");
        }
        test::write(root + "/introsecs/manifest.json", "{\"format\":1,\"id\":\"introsecs\",\"name\":\"Intro Sections\"}");
        test::write(root + "/introsecs/kin.json",
                    "{\"intro\":{\"body\":\"General kin text.\",\"sections\":[{\"name\":\"A Section\",\"body\":\"Section body.\"}]},\"kin\":[{\"name\":\"Homebrew Kin\"}]}");
        ContentStore is;
        is.load({PackSpec{coreDir, true, true}, PackSpec{root + "/introsecs", false, true}});
        const Intro& kinIntro = is.introOf(Kind::Kin);
        check(kinIntro.body == "General kin text." && kinIntro.sections.size() == 1 && kinIntro.sections[0].title == "A Section" &&
                  kinIntro.sections[0].body == "Section body.",
              "an \"intro\" written as an object (body + sections) is read the same way a rule's is");

        // tables that belong to a card
        test::write(root + "/cardtabs/manifest.json", "{\"format\":1,\"id\":\"cardtabs\",\"name\":\"Card Tables\"}");
        test::write(root + "/cardtabs/spells.json",
                    "{\"spells\":[{\"name\":\"Charm\",\"tables\":[{\"name\":\"Side effects\",\"dice\":\"D4\",\"rows\":[\"a\",\"b\",\"c\",\"d\"]},{\"rows\":[\"x\"]}]}]}");
        test::write(root + "/cardtabs/kin.json",
                    "[{\"name\":\"Sprite\",\"tables\":[{\"name\":\"Sprite: Names\",\"dice\":\"D2\",\"columns\":[\"NAME\"],\"rows\":[{\"roll\":\"1\",\"cells\":[\"Pip\"]},{\"roll\":\"2\",\"cells\":[\"Pop\"]}]}]},"
                    "{\"name\":\"Pixie\",\"names\":[\"Own\"],\"tables\":[{\"name\":\"Pixie: Names\",\"rows\":[[\"Table\"]]}]}]");
        ContentStore x;
        x.load({PackSpec{coreDir, true, true}, PackSpec{root + "/cardtabs", false, true}});
        const PackInfo* ct = x.pack("cardtabs");
        const Entry* charm = x.findByName(Kind::Spell, "Charm");
        check(ct && ct->loaded && ct->warnings.size() == 1 && hasWarning(*ct, "has no \"name\"") && charm && charm->tables.size() == 1 && charm->tables[0].title == "Side effects" &&
                  charm->tables[0].dieSides() == 4 && charm->tables[0].rows.size() == 4 && charm->tables[0].key == charm->key + "#1",
              "any card can carry tables; one without a name is skipped with a warning");
        const Entry* sprite = x.findByName(Kind::Kin, "Sprite");
        const Entry* pixie = x.findByName(Kind::Kin, "Pixie");
        check(sprite && sprite->list("names") == std::vector<std::string>{"Pip", "Pop"} && pixie && pixie->list("names") == std::vector<std::string>{"Own"},
              "a kin without a list of names takes it from its table; one that writes its own list keeps it");

        // a card can be replaced too, in place, like a rule ("Generate Kin" > "Edit" writes this)
        const std::string humanKey = core.entry(Kind::Kin, core.idByKey(Kind::Kin, "core/kin/human"))->key;
        test::write(root + "/custom-kin/kin.json",
                    "{\"name\":\"Custom Kin\",\"kin\":[{\"name\":\"Homebrew Orc\",\"movement\":8,\"innate_abilities\":[\"Tough\"]},"
                    "{\"name\":\"Human\",\"replaces\":\"" +
                        humanKey + "\",\"description\":\"Reskinned.\",\"movement\":12,\"innate_abilities\":[\"Adaptive\",\"Brave\"],\"image\":\"images/human.png\"}]}");
        test::write(root + "/custom-kin/images/human.png", "not a real png, just needs to exist");
        ContentStore cx;
        cx.load({PackSpec{coreDir, true, true}, PackSpec{root + "/custom-kin", false, true}});
        const Entry* orc = cx.findByName(Kind::Kin, "Homebrew Orc");
        check(orc && orc->key == "custom-kin/kin/homebrew-orc" && orc->prop("movement") == "8" && orc->list("innate_abilities") == std::vector<std::string>{"Tough"},
              "a new custom kin gets its own key in its own pack");
        const int humanId = cx.idByKey(Kind::Kin, humanKey);
        const Entry* editedHuman = cx.entry(Kind::Kin, humanId);
        check(editedHuman && editedHuman->key == humanKey && editedHuman->body.starts_with("Reskinned.") && editedHuman->prop("movement") == "12" &&
                  editedHuman->list("innate_abilities") == (std::vector<std::string>{"Adaptive", "Brave"}) && editedHuman->editedBy == "Custom Kin" &&
                  !editedHuman->image.empty() && editedHuman->image.ends_with("images/human.png"),
              "replacing a card keeps its key and id but takes the new pack's text, picture and \"Changed by\" (its body still gets its "
              "innate abilities' text appended after, like any kin's)");
        check(cx.count(Kind::Kin) == core.count(Kind::Kin) + 1, "replacing a card does not add a new one, only the brand-new kin does");

        // a pack that fails to load leaves the rules of the others alone, and an off pack changes nothing
        test::write(root + "/broken/manifest.json", "{\"format\":1,\"id\":\"broken-rules\",\"name\":\"Broken\"}");
        test::write(root + "/broken/spells.json", "{ nope");
        test::write(root + "/broken/rules.json", "[{\"name\":\"Never\",\"body\":\"x\",\"replaces\":\"core/rule/melee-combat\"}]");
        ContentStore t;
        t.load({PackSpec{coreDir, true, true}, PackSpec{root + "/broken", false, true}, PackSpec{root + "/house", false, false}});
        check(t.pack("broken-rules") && !t.pack("broken-rules")->loaded && t.rule(t.ruleByKey(melee)) && t.rule(t.ruleByKey(melee))->editedBy.empty() &&
                  t.rules().size() == core.rules().size(),
              "a broken pack replaces nothing, and a pack that is off adds nothing");
    }

    // ------------------------------------------------------------------------------- packs without a manifest.json
    {
        const std::string root = test::scratch("nomanifest");
        removeTree(root);
        test::write(root + "/my-tome/spells.json", "{\"name\":\"My Tome\",\"author\":\"Me\",\"version\":\"2\",\"spells\":[{\"name\":\"Zap\",\"page\":12,\"printed_page\":99}]}");
        test::write(root + "/plain/kin.json", "[{\"name\":\"Gnome\"}]");
        test::write(root + "/Bad Name/kin.json", "[{\"name\":\"Nope\"}]");
        test::write(root + "/empty/readme.txt", "x");
        test::write(root + "/both/manifest.json", "{\"format\":1,\"id\":\"both\",\"name\":\"From Manifest\"}");
        test::write(root + "/both/spells.json", "{\"name\":\"From Header\",\"spells\":[{\"name\":\"Puff\"}]}");
        test::write(root + "/late/kin.json", "[{\"name\":\"Pixie\"}]");                                   // no header here...
        test::write(root + "/late/skills.json", "{\"name\":\"Late Header\",\"skills\":[{\"name\":\"Sneak\"}]}");   // ...it is in the next file
        ContentStore s;
        s.load({PackSpec{coreDir, true, true}, PackSpec{root + "/my-tome", false, true}, PackSpec{root + "/plain", false, true}, PackSpec{root + "/Bad Name", false, true},
                PackSpec{root + "/empty", false, true}, PackSpec{root + "/both", false, true}, PackSpec{root + "/late", false, true}});
        const PackInfo* tome = s.pack("my-tome");
        check(tome && tome->loaded && tome->name == "My Tome" && tome->author == "Me" && tome->version == "2" && s.findByName(Kind::Spell, "Zap"),
              "a pack with no manifest.json: its id is its folder's name and its description is the header of its data file");
        const Entry* zap = s.findByName(Kind::Spell, "Zap");
        check(zap && zap->pageNote == "p.12" && !zap->ref.valid(), "an entry says its page once, with \"page\": a printed_page written next to it is not used");
        const PackInfo* plain = s.pack("plain");
        check(plain && plain->loaded && plain->name == "plain" && s.findByName(Kind::Kin, "Gnome"), "with no header either, the name is the id");
        const PackInfo* late = s.pack("late");
        check(late && late->loaded && late->name == "Late Header" && s.findByName(Kind::Kin, "Pixie"), "the header can be in any of the data files: the first one that has it counts");
        const PackInfo* both = s.pack("both");
        check(both && both->name == "From Manifest" && s.findByName(Kind::Spell, "Puff"), "a manifest.json, when there is one, wins over a header");
        bool badName = false, empty = false;
        for (const PackInfo& p : s.packs()) {
            badName |= p.dir.contains("Bad Name") && !p.loaded && p.error.contains("manifest.json") && p.error.contains("folder name");
            empty |= p.dir.contains("empty") && !p.loaded && p.error.contains("manifest.json");
        }
        check(badName, "with no manifest.json, a folder name that is not a valid id is explained");
        check(empty, "a folder with no manifest.json and no data file is not a pack");
        check(looksLikePack(root + "/my-tome") && looksLikePack(root + "/both") && !looksLikePack(root + "/empty"), "looksLikePack: a manifest.json or any data file");
        PackManager pm(coreDir, root);
        std::set<std::string> found;
        for (const PackSpec& spec : pm.specs({})) found.insert(spec.dir.substr(spec.dir.find_last_of('/') + 1));
        check(found.count("my-tome") && found.count("plain") && found.count("both") && !found.count("empty"), "a folder of the user's packs counts as a pack with or without manifest.json");
    }

    // ---------------------------------------------------------------------------- bad packs
    {
        const std::string root = test::scratch("badpacks");
        removeTree(root);
        auto manifest = [](const std::string& id) { return "{\"format\":1,\"id\":\"" + id + "\",\"name\":\"" + id + "\"}"; };
        test::write(root + "/badjson/manifest.json", manifest("badjson"));
        test::write(root + "/badjson/spells.json", "{\"spells\": [ {\"name\": \"Oops\" ");
        test::write(root + "/nonames/manifest.json", manifest("nonames"));
        test::write(root + "/nonames/spells.json", "{\"spells\":[{\"school\":\"x\"},{\"name\":\"Fine\",\"description\":\"ok\"},5]}");
        test::write(root + "/dupes/manifest.json", manifest("dupes"));
        test::write(root + "/dupes/spells.json", "[{\"id\":\"a\",\"name\":\"One\"},{\"id\":\"a\",\"name\":\"Two\"}]");
        test::write(root + "/paths/manifest.json", manifest("paths"));
        test::write(root + "/paths/creatures.json",
                    "{\"creatures\":[{\"name\":\"Sneaky\",\"image\":\"../../secret.png\"},{\"name\":\"Lost\",\"image\":\"images/none.png\"},"
                    "{\"name\":\"Abs\",\"image\":\"C:/Windows/x.png\"}]}");
        test::write(root + "/newer/manifest.json", "{\"format\":99,\"id\":\"newer\",\"name\":\"n\"}");
        test::write(root + "/badid/manifest.json", manifest("Bad Id!"));
        test::write(root + "/nomani/readme.txt", "x");
        test::write(root + "/wrongshape/manifest.json", manifest("wrongshape"));
        test::write(root + "/wrongshape/spells.json", "{\"spells\": \"not a list\"}");
        test::write(root + "/twin1/manifest.json", manifest("twin"));
        test::write(root + "/twin1/spells.json", "[{\"name\":\"First twin\"}]");
        test::write(root + "/twin2/manifest.json", manifest("twin"));
        test::write(root + "/twin2/spells.json", "[{\"name\":\"Second twin\"}]");
        test::write(root + "/bom/manifest.json", "\xEF\xBB\xBF" + manifest("bom"));
        test::write(root + "/bom/spells.json", "\xEF\xBB\xBF// comments are fine\n[{\"name\":\"With BOM\"}]");

        ContentStore s;
        std::vector<PackSpec> specs = {{coreDir, true, true}};
        for (const char* n : {"badjson", "nonames", "dupes", "paths", "newer", "badid", "nomani", "wrongshape", "twin1", "twin2", "bom"})
            specs.push_back({root + "/" + n, false, true});
        s.load(specs);

        const PackInfo* badjson = s.pack("badjson");
        check(badjson && !badjson->loaded && badjson->error.contains("spells.json") && s.findByName(Kind::Spell, "Oops") == nullptr,
              "broken JSON: the pack is rejected, the error names the file");
        check(s.pack("core") && s.pack("core")->loaded && s.count(Kind::Spell) >= 66, "a broken pack does not touch the others");
        const PackInfo* nonames = s.pack("nonames");
        check(nonames && nonames->loaded && nonames->counts[static_cast<int>(Kind::Spell)] == 1 && nonames->warnings.size() == 2 &&
                  s.findByName(Kind::Spell, "Fine"),
              "entries without a name are skipped with a warning, the rest loads");
        const PackInfo* dupes = s.pack("dupes");
        check(dupes && dupes->loaded && dupes->counts[static_cast<int>(Kind::Spell)] == 2 && hasWarning(*dupes, "used twice") &&
                  s.findByName(Kind::Spell, "Two")->key == "dupes/spell/a-2",
              "duplicate ids are made unique and reported");
        const PackInfo* paths = s.pack("paths");
        const Monster* sneaky = monsterOf(s, "Sneaky", "paths");
        check(paths && paths->loaded && sneaky && sneaky->image.empty() && hasWarning(*paths, "inside the pack folder") && hasWarning(*paths, "not found"),
              "image paths that escape the pack (or do not exist) are ignored with a warning");
        check(monsterOf(s, "Abs", "paths") && monsterOf(s, "Abs", "paths")->image.empty(), "absolute image paths are ignored");
        bool newer = false;
        for (const PackInfo& p : s.packs()) newer |= (p.dir.contains("newer") && !p.loaded && p.error.contains("newer version"));
        check(newer, "a pack from a newer format is refused");
        const PackInfo* badid = s.pack("Bad Id!");
        check(badid == nullptr || !badid->loaded, "invalid pack ids are refused");
        bool anyBadId = false;
        for (const PackInfo& p : s.packs()) anyBadId |= (p.dir.contains("badid") && !p.loaded && !p.error.empty());
        check(anyBadId, "the invalid id is explained");
        bool noMani = false;
        for (const PackInfo& p : s.packs()) noMani |= (p.dir.contains("nomani") && !p.loaded && p.error.contains("manifest.json"));
        check(noMani, "a folder without manifest.json is reported");
        const PackInfo* shape = s.pack("wrongshape");
        check(shape && !shape->loaded && shape->error.contains("expected a list"), "a file of the wrong shape is refused");
        check(s.findByName(Kind::Spell, "First twin") && !s.findByName(Kind::Spell, "Second twin"), "a second pack with the same id is refused");
        check(s.findByName(Kind::Spell, "With BOM"), "UTF-8 BOM and // comments are accepted");
    }

    // -------------------------------------------------------------------------------- importing
    {
        const std::string userDir = test::scratch("userpacks");
        removeTree(userDir);
        PackManager pm(coreDir, userDir);
        const std::string fixtures = test::sourceDir() + "/tests/fixtures";

        ImportResult r = pm.import(example);
        check(r.ok && r.packId == "frostmarch" && !r.replaced && r.entries > 10, "import a folder: " + r.message);
        check(test::exists(userDir + "/frostmarch/manifest.json") && test::exists(userDir + "/frostmarch/images/frost-wight.png"),
              "the pack (with its images) is copied to the user's folder");
        r = pm.import(example);
        check(r.ok && r.replaced, "importing the same pack again updates it");
        auto specs = pm.specs({});
        check(specs.size() == 2 && specs[0].core && specs[0].dir.ends_with("core") && specs[1].dir.contains("frostmarch") && specs[1].enabled,
              "installed packs are listed after Core");
        specs = pm.specs({"frostmarch"});
        check(specs.size() == 2 && !specs[1].enabled, "a disabled pack id switches its spec off");

        r = pm.import(fixtures + "/frostmarch-tales.zip");
        check(r.ok && r.replaced && r.packId == "frostmarch", "import a .zip that holds the pack folder: " + r.message);
        r = pm.import(fixtures + "/frostmarch-flat.zip");
        check(r.ok && r.packId == "frostmarch", "import a .zip with manifest.json at its top: " + r.message);
        check(test::exists(userDir + "/frostmarch/images/frost-wight.png"), "images are unpacked from the zip");

        const size_t before = pm.specs({}).size();
        SDL_RemovePath((userDir + "/../evil.json").c_str());          // (another test may have left a file there)
        r = pm.import(fixtures + "/evil.zip");
        check(!r.ok && r.message.contains("unsafe"), "a zip with ../ paths is refused: " + r.message);
        check(!test::exists(userDir + "/evil.json") && !test::exists(userDir + "/../evil.json") && pm.specs({}).size() == before,
              "nothing escaped the install folder, nothing was installed");
        r = pm.import(fixtures + "/no-manifest.zip");
        check(!r.ok && r.message.contains("manifest.json"), "a zip without manifest.json is refused");
        r = pm.import(userDir + "/nowhere");
        check(!r.ok, "a path that does not exist is refused");
        r = pm.import(test::sourceDir() + "/README.md");
        check(!r.ok, "a file that is not a zip is refused");

        const std::string reserved = test::scratch("reserved");
        removeTree(reserved);
        test::write(reserved + "/manifest.json", "{\"format\":1,\"id\":\"core\",\"name\":\"Fake core\"}");
        r = pm.import(reserved);
        check(!r.ok && r.message.contains("reserved"), "the id 'core' cannot be taken over");

        const std::string broken = test::scratch("brokenimport");
        removeTree(broken);
        test::write(broken + "/manifest.json", "{\"format\":1,\"id\":\"broken-one\",\"name\":\"Broken\"}");
        test::write(broken + "/kin.json", "{ nope");
        r = pm.import(broken);
        check(!r.ok && !test::exists(userDir + "/broken-one") && r.message.contains("kin.json"), "a pack that does not load is not installed: " + r.message);

        std::string why;
        check(!pm.remove("core", &why) && !pm.remove("../x", &why) && !pm.remove("nothere", &why),
              "the built-in packs and odd ids cannot be removed");
        check(pm.remove("frostmarch", &why) && !test::exists(userDir + "/frostmarch"), "an installed pack can be removed");

        check(isSafeArchivePath("images/a.png") && !isSafeArchivePath("../a") && !isSafeArchivePath("/a") && !isSafeArchivePath("a/../../b") &&
                  !isSafeArchivePath("C:/a") && !isSafeArchivePath(""),
              "archive path check");
    }

    // ------------------------------------------------------------------- editing packs while the app is open
    {
        const std::string root = test::scratch("livepack");
        removeTree(root);
        const std::string dir = root + "/tome";
        test::write(dir + "/manifest.json", "{\"format\":1,\"id\":\"tome\",\"name\":\"Tome\"}");
        test::write(dir + "/kin.json", "[]");
        test::write(dir + "/images/a.png", "x");
        const std::vector<PackSpec> on = {PackSpec{coreDir, true, true}, PackSpec{dir, false, true}};
        const std::string before = packSignature(on);
        check(!before.empty() && before == packSignature(on), "a pack that did not change has the same signature");

        test::write(dir + "/kin.json", "[{\"name\": \"Elf\"}]");
        const std::string edited = packSignature(on);
        check(edited != before, "editing a pack's file changes the signature");
        test::write(dir + "/spells.json", "[]");
        check(packSignature(on) != edited, "so does a file that was not there");
        const std::string withSpells = packSignature(on);
        test::write(dir + "/images/b.png", "yy");
        check(packSignature(on) != withSpells, "and a picture added to a homebrew pack");
        const std::string withArt = packSignature(on);
        check(packSignature({PackSpec{coreDir, true, true}, PackSpec{dir, false, false}}) != withArt, "and switching the pack off");
        check(packSignature({PackSpec{coreDir, true, true}}) != withArt, "and the pack going away");
        removeTree(root);
    }

    return test::finish();
}
