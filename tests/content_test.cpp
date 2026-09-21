// The book database, the Core content pack, homebrew packs (loading, validation, importing) and search.
// Needs data/skaldbok.db and data/packs/core (tools/build_db.py makes both).
#include <set>
#include <string>
#include <vector>

#include "content.h"
#include "db.h"
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
    Database db;
    std::string err;
    if (!db.open(dataDir + "/skaldbok.db", &err)) {
        std::printf("cannot open database: %s\n", err.c_str());
        return 2;
    }
    const std::string coreDir = dataDir + "/packs/core";
    const std::string example = test::sourceDir() + "/examples/frostmarch-tales";

    // ------------------------------------------------------------------------------- the book database
    check(db.sources().size() == 3, "three book sources");

    // ---------------------------------------------------------------------------------- the Core pack
    ContentStore core;
    core.load(db, {PackSpec{coreDir, true, true}});
    check(core.packs().size() == 1 && core.packs()[0].loaded && core.packs()[0].error.empty(), "Core pack loads: " + core.packs()[0].error);
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
    const Entry* mage = find(core, Kind::Profession, "Mage");
    check(mage && mage->list("schools").size() == 3 && mage->prop("magic") == "1" && mage->list("skills.Animism").size() == 8,
          "Mage: three schools, 8 skills each, starts with magic");
    const Entry* fighter = find(core, Kind::Profession, "Fighter");
    check(fighter && fighter->list("skills").size() == 8 && fighter->list("starting_gear").size() == 3 &&
              fighter->list("heroic_abilities") == std::vector<std::string>{"Veteran"},
          "Fighter: 8 skills, 3 gear sets, heroic ability Veteran");
    const Entry* axes = find(core, Kind::Skill, "Axes");
    check(axes && axes->prop("attribute") == "STR" && find(core, Kind::Skill, "Bows")->prop("attribute") == "AGL", "weapon skills know their attribute");
    const Entry* fetch = find(core, Kind::Spell, "Fetch");
    check(fetch && fetch->prop("trick") == "1" && fetch->prop("school") == "General Magic", "Fetch is a General Magic trick");

    // keys survive: a saved key finds the same entry again
    check(human && core.idByKey(Kind::Kin, human->key) == human->id && human->key == "core/kin/human", "stable keys: core/kin/human");
    check(centaur && core.idByKey(Kind::Monster, core.keyOf(Kind::Monster, centaur->id)) == centaur->id, "creature key round-trips");
    check(core.keyOf(Kind::Table, 12) == "#12" && core.idByKey(Kind::Table, "#12") == 12, "book tables keep numeric keys");

    // tables the character creator rolls on
    const DataTable* weakness = core.tableByRole("weakness");
    check(weakness && weakness->dieSides() == 20 && weakness->rows.size() == 20 && !weakness->browse, "Weakness role table: D20, 20 rows, not listed twice");
    bool everyRoll = weakness != nullptr;
    for (int r = 1; weakness && r <= 20; ++r) everyRoll &= weakness->rowForRoll(r) >= 0;
    check(everyRoll, "every D20 roll maps to a row");
    check(core.tableByRole("memento") && core.tableByRole("appearance"), "memento and appearance tables exist");
    DataTable dbTable;
    int weaknessId = 0;
    for (const ListItem& it : db.listTables())
        if (it.name == "Weakness") weaknessId = it.id;
    check(weaknessId && db.table(weaknessId, dbTable) && dbTable.rows.size() == 20, "book tables still come from the database");

    // search: content and the books' tables
    auto hits = mergeHits(core.search("centaur attacks"), db.search("centaur attacks"), 60);
    bool centaurHit = false;
    for (const Hit& h : hits) centaurHit |= h.kind == Kind::Monster && h.title == "Centaur";
    check(centaurHit, "search 'centaur attacks' finds the Centaur creature");
    check(!mergeHits(core.search("dragonslayer"), db.search("dragonslayer"), 60).empty(), "prefix search 'dragonslayer'");
    hits = mergeHits(core.search("fetch"), db.search("fetch"), 60);
    check(!hits.empty() && hits[0].exact && hits[0].title == "Fetch", "an exact title comes first");
    check(core.search("").empty() && core.search("   ").empty(), "empty search finds nothing");
    for (const Hit& h : db.search("centaur"))
        if (h.kind != Kind::Table) check(false, "the book index only returns tables");

    // Rules: book text minus what has its own category (creatures, spells, skills, kin...), the adventure and the indexes.
    {
        const std::vector<RuleNode> rules = db.listRules();
        std::set<std::string> titles;
        std::set<int> parents;
        for (const RuleNode& n : rules) parents.insert(n.parent);
        for (const RuleNode& n : rules) {
            titles.insert(n.title);
            if (n.body.empty() && !parents.count(n.id)) check(false, "a rule heading without text or children was kept");
            if (n.ref.sourceId == db.sourceIdByKey("adventure")) check(false, "the adventure is not part of Rules");
        }
        check(rules.size() > 100 && rules.size() < 700, "a review-sized set of rules text");
        check(titles.count("Boons & Banes") && titles.count("Melee Combat"), "general rules are kept");
        check(!titles.count("Contents") && !titles.count("Index"), "contents and index are dropped");
        check(!titles.count("Dragon") && !titles.count("Fireball"), "creatures and spells are not repeated");
    }

    // ------------------------------------------------------------------------------------ homebrew
    {
        ContentStore s;
        s.load(db, {PackSpec{coreDir, true, true}, PackSpec{example, false, true}});
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
        hits = mergeHits(s.search("frost wight"), db.search("frost wight"), 20);
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
        s.load(db, {PackSpec{coreDir, true, true}, PackSpec{example, false, false}});
        const PackInfo* hb = s.pack("frostmarch");
        check(hb && !hb->enabled && !hb->loaded && hb->total() == 0 && s.count(Kind::Spell) == 66, "a disabled pack is listed, nothing of it is loaded");
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
        s.load(db, specs);

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

        ImportResult r = pm.import(example, db);
        check(r.ok && r.packId == "frostmarch" && !r.replaced && r.entries > 10, "import a folder: " + r.message);
        check(test::exists(userDir + "/frostmarch/manifest.json") && test::exists(userDir + "/frostmarch/images/frost-wight.png"),
              "the pack (with its images) is copied to the user's folder");
        r = pm.import(example, db);
        check(r.ok && r.replaced, "importing the same pack again updates it");
        auto specs = pm.specs({});
        check(specs.size() == 2 && specs[0].core && specs[1].dir.contains("frostmarch") && specs[1].enabled, "installed packs are listed after Core");
        specs = pm.specs({"frostmarch"});
        check(specs.size() == 2 && !specs[1].enabled, "a disabled pack id switches its spec off");

        r = pm.import(fixtures + "/frostmarch-tales.zip", db);
        check(r.ok && r.replaced && r.packId == "frostmarch", "import a .zip that holds the pack folder: " + r.message);
        r = pm.import(fixtures + "/frostmarch-flat.zip", db);
        check(r.ok && r.packId == "frostmarch", "import a .zip with manifest.json at its top: " + r.message);
        check(test::exists(userDir + "/frostmarch/images/frost-wight.png"), "images are unpacked from the zip");

        const size_t before = pm.specs({}).size();
        SDL_RemovePath((userDir + "/../evil.json").c_str());          // (another test may have left a file there)
        r = pm.import(fixtures + "/evil.zip", db);
        check(!r.ok && r.message.contains("unsafe"), "a zip with ../ paths is refused: " + r.message);
        check(!test::exists(userDir + "/evil.json") && !test::exists(userDir + "/../evil.json") && pm.specs({}).size() == before,
              "nothing escaped the install folder, nothing was installed");
        r = pm.import(fixtures + "/no-manifest.zip", db);
        check(!r.ok && r.message.contains("manifest.json"), "a zip without manifest.json is refused");
        r = pm.import(userDir + "/nowhere", db);
        check(!r.ok, "a path that does not exist is refused");
        r = pm.import(test::sourceDir() + "/README.md", db);
        check(!r.ok, "a file that is not a zip is refused");

        const std::string reserved = test::scratch("reserved");
        removeTree(reserved);
        test::write(reserved + "/manifest.json", "{\"format\":1,\"id\":\"core\",\"name\":\"Fake core\"}");
        r = pm.import(reserved, db);
        check(!r.ok && r.message.contains("reserved"), "the id 'core' cannot be taken over");

        const std::string broken = test::scratch("brokenimport");
        removeTree(broken);
        test::write(broken + "/manifest.json", "{\"format\":1,\"id\":\"broken-one\",\"name\":\"Broken\"}");
        test::write(broken + "/kin.json", "{ nope");
        r = pm.import(broken, db);
        check(!r.ok && !test::exists(userDir + "/broken-one") && r.message.contains("kin.json"), "a pack that does not load is not installed: " + r.message);

        std::string why;
        check(!pm.remove("core", &why) && !pm.remove("../x", &why) && !pm.remove("nothere", &why), "core and odd ids cannot be removed");
        check(pm.remove("frostmarch", &why) && !test::exists(userDir + "/frostmarch"), "an installed pack can be removed");

        check(isSafeArchivePath("images/a.png") && !isSafeArchivePath("../a") && !isSafeArchivePath("/a") && !isSafeArchivePath("a/../../b") &&
                  !isSafeArchivePath("C:/a") && !isSafeArchivePath(""),
              "archive path check");
    }

    return test::finish();
}
