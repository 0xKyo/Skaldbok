// Editing a sheet from two places: field-level sets, the rules that flag (not refuse), the GM's rulings, the change log, and a store
// that folds a save into a file somebody else changed in the meantime.
#include <algorithm>
#include <set>
#include <string>

#include "changelog.h"
#include "character.h"
#include "fsutil.h"
#include "packs.h"
#include "sheet_edit.h"
#include "testutil.h"

using namespace gm;
using test::check;

namespace {

Character sample() {
    Character c;
    c.id = "c-test";
    c.name = "Brenna";
    c.player = "Sebastián";
    c.kin = {"core/kin/human", "Human"};
    c.profession = {"core/profession/fighter", "Fighter"};
    const int a[6] = {15, 13, 12, 9, 10, 11};
    std::copy(a, a + 6, c.attr);
    c.hp = 13;
    c.wp = 10;
    c.skills = {{{"core/skill/axes", "Axes"}, "STR", 12, true, false}};
    c.abilities = {{"core/ability/veteran", "Veteran"}};
    c.inventory = {{"Torch", "", 1, ""}, {"Rope", "", 1, ""}};
    c.silver = 3;
    return c;
}

json toDoc(const Character& c) {
    json j;
    jsonParse(c.toJson(), j, nullptr);
    return j;
}

json parse(const std::string& text) {
    json j;
    jsonParse(text, j, nullptr);
    return j;
}

}  // namespace

int main() {
    const std::string root = test::scratch("sheet") + "/";
    removeTree(root);
    SDL_CreateDirectory(root.c_str());

    // ----------------------------------------------------------------------------------------- sets
    json doc = toDoc(sample());
    sheet::Applied a = sheet::applySet(doc, parse(R"({"hp": 7, "attributes": {"STR": 16}, "coins": {"gold": 2}})"), true);
    check(a.ok && doc["hp"] == 7 && doc["attributes"]["STR"] == 16 && doc["attributes"]["CON"] == 13 && doc["coins"]["gold"] == 2 && doc["coins"]["silver"] == 3,
          "a set changes fields, and attributes and coins merge key by key");
    check(a.keys.size() == 3, "it says which fields changed");
    check(sheet::applySet(doc, parse(R"({"hp": 7})"), true).keys.empty(), "setting what is already there changes nothing");
    check(sheet::applySet(doc, parse(R"({"inventory": [{"name": "Rope", "count": 3}]})"), true).ok && doc["inventory"].size() == 1, "a list replaces the whole list");

    for (const char* bad : {R"({"kin": {"name": "Elf"}})", R"({"profession": "Mage"})", R"({"id": "c-other"})", R"({"revision": 99})", R"({"locked": false})",
                            R"({"reviews": {}})", R"({"created_at": "x"})", R"({"player": "someone"})", R"({"school": "x"})"}) {
        check(!sheet::applySet(doc, parse(bad), true).ok, std::string("a player cannot change: ") + bad);
    }
    for (const char* bad : {R"({"hp": "ten"})", R"({"hp": 7.5})", R"({"attributes": {"LUCK": 3}})", R"({"attributes": {"STR": "big"}})", R"({"age": "ancient"})",
                            R"({"conditions": ["Sleepy"]})", R"({"inventory": [{"count": 2}]})", R"({"inventory": [{"name": "x", "count": 0}]})", R"({"skills": [{"level": 3}]})",
                            R"({"skills": "all"})", R"({"coins": {"platinum": 1}})", R"({"coins": {"gold": -1}})", R"({"notes": 5})", R"({"nonsense": 1})", R"([1])"}) {
        check(!sheet::applySet(doc, parse(bad), true).ok, std::string("wrong types and sizes are refused: ") + bad);
    }
    const json before = doc;
    check(!sheet::applySet(doc, parse(R"({"hp": 1, "attributes": {"STR": "big"}})"), true).ok && doc == before, "a refused set changes nothing at all, not even its good parts");
    json huge = json::array();
    for (int i = 0; i < 101; ++i) huge.push_back({{"name", "x"}});
    json setBig = json::object();
    setBig["inventory"] = huge;
    check(!sheet::applySet(doc, setBig, true).ok, "lists have a size limit");
    check(sheet::applySet(doc, parse(R"({"locked": true, "reviews": {"encumbrance": {"status": "approved", "value": "9/8"}}})"), false).ok && doc["locked"] == true && doc["reviews"].size() == 1,
          "the GM's own path may also set the lock and the rulings");
    check(!sheet::applySet(doc, parse(R"({"reviews": 5})"), false).ok && !sheet::applySet(doc, parse(R"({"locked": "yes"})"), false).ok, "and those are checked too");

    // ------------------------------------------------------------------------------ diff and merge
    const Character c0 = sample();
    Character c1 = c0;
    c1.hp = 9;
    c1.attr[0] = 16;
    c1.notes = "Owes the innkeeper.";
    c1.reviews["encumbrance"] = {"approved", "9/8", "2026-01-01T00:00:00Z"};
    const json d = sheet::diff(toDoc(c0), toDoc(c1));
    check(d.size() == 4 && d["hp"] == 9 && d["attributes"].size() == 1 && d["attributes"]["STR"] == 16 && d["reviews"]["encumbrance"]["status"] == "approved", "a diff holds only what changed, key by key inside attributes");
    json applied = toDoc(c0);
    sheet::merge(applied, d);
    check(applied["hp"] == 9 && applied["attributes"]["STR"] == 16 && applied["notes"] == "Owes the innkeeper." && applied["reviews"].size() == 1, "merging a diff makes the same change");
    json removed = applied;
    sheet::merge(removed, parse(R"({"reviews": {"encumbrance": null}})"));
    check(!removed.contains("reviews"), "a null inside reviews removes that ruling, and an empty list of rulings disappears");
    check(sheet::diff(toDoc(c0), toDoc(c0)).empty(), "nothing changed, nothing in the diff (dates and revisions do not count)");

    // ----------------------------------------------------------------------------------- describing
    json was = parse(R"({"hp": 10, "attributes": {"STR": 15}, "conditions": ["Angry"], "inventory": [{"name": "Torch"}], "coins": {"gold": 0}})");
    json now = parse(R"({"hp": 7, "attributes": {"STR": 16}, "conditions": ["Scared"], "inventory": [{"name": "Torch"}, {"name": "Rope", "count": 2}], "coins": {"gold": 3}})");
    const std::string said = sheet::describe(was, now);
    check(said.find("HP 10 → 7") != std::string::npos && said.find("STR 15 → 16") != std::string::npos && said.find("+Scared") != std::string::npos && said.find("-Angry") != std::string::npos &&
              said.find("+Rope ×2") != std::string::npos && said.find("Gold 0 → 3") != std::string::npos,
          "a change is described in words: " + said);
    const std::string skills = sheet::describe(parse(R"({"skills": [{"name": "Axes", "level": 12, "trained": true, "marked": false}]})"),
                                               parse(R"({"skills": [{"name": "Axes", "level": 13, "trained": true, "marked": true}, {"name": "Bows", "level": 6}]})"));
    check(skills.find("Axes 12 → 13") != std::string::npos && skills.find("Axes marked") != std::string::npos && skills.find("+Bows 6") != std::string::npos, "skills: levels, marks and new ones: " + skills);

    // ------------------------------------------------------------------------------------ the rules
    Character rules = sample();
    check(sheet::rulesIssues(rules).empty() && sheet::openIssues(rules).empty(), "a normal sheet has nothing to flag");
    rules.hp = 20;
    rules.attr[3] = 19;
    rules.attr[4] = 2;
    rules.skills[0].level = 20;
    rules.hpBonus = 3;
    for (int i = 0; i < 10; ++i) rules.inventory.push_back({"Stone " + std::to_string(i), "", 1, ""});
    std::set<std::string> keys;
    for (const sheet::Issue& i : sheet::rulesIssues(rules)) keys.insert(i.key);
    check(keys == std::set<std::string>{"hp", "wp", "hpmax", "attr:INT", "attr:WIL", "skill:Axes", "encumbrance"}, "every kind of rule break is found (a WIL of 2 also leaves WP above its maximum)");
    rules.abilities.push_back({"core/ability/robust", "Robust"});
    const std::vector<sheet::Issue> withRobust = sheet::rulesIssues(rules);
    check(!withRobust.empty() && std::none_of(withRobust.begin(), withRobust.end(), [](const sheet::Issue& i) { return i.key == "hpmax"; }), "the ability that allows it makes it normal");

    Character judged = sample();
    for (int i = 0; i < 10; ++i) judged.inventory.push_back({"Stone", "", 1, ""});
    check(sheet::openIssues(judged).size() == 1 && sheet::openIssues(judged)[0].key == "encumbrance" && sheet::openIssues(judged)[0].status == "pending", "a broken rule starts as pending");
    const std::string carried = sheet::openIssues(judged)[0].value;
    check(sheet::review(judged, "encumbrance", false) && sheet::openIssues(judged)[0].status == "rejected", "the GM rejects it: it stays flagged, marked as rejected");
    check(sheet::review(judged, "encumbrance", true) && sheet::openIssues(judged).empty(), "the GM approves it: it is normal now");
    judged.inventory.push_back({"One more stone", "", 1, ""});
    check(sheet::openIssues(judged).size() == 1 && sheet::openIssues(judged)[0].status == "pending" && sheet::openIssues(judged)[0].value != carried, "a worse case asks again: the ruling was for exactly what was judged");
    sheet::prune(judged);
    check(judged.reviews.empty(), "a ruling that no longer matches is forgotten");
    check(!sheet::review(judged, "attr:STR", true), "there is nothing to rule on where the rules are not broken");

    // ------------------------------------------------------------------------------- the file itself
    ChangeLog log;
    log.setPrefDir(root);
    const std::string path = root + "c-test.json";
    Character seed = sample();
    seed.revision = 4;
    test::write(path, seed.toJson());
    check(log.poll().empty(), "the log's first look only takes note");

    sheet::EditResult r = sheet::editFile(path, "c-test", parse(R"({"hp": 7, "inventory": [{"name": "Torch"}, {"name": "Rope"}, {"name": "Lantern"}]})"), true, &log);
    check(r.ok && r.status == 200 && r.character.hp == 7 && r.character.inventory.size() == 3 && r.character.revision == 5 && r.keys.size() == 2, "a player edit is applied to the file, revision + 1");
    Character onDisk;
    Character::fromJson(fs::readFile(path).value_or(""), onDisk, nullptr);
    check(onDisk.hp == 7 && onDisk.revision == 5 && onDisk.player == "Sebastián" && onDisk.kin.name == "Human" && onDisk.updatedAt.size() == 20, "and written back whole, with everything else as it was");
    const auto entries = log.entries("c-test");
    check(entries.size() == 1 && entries[0].by == "player" && entries[0].summary.find("HP 13 → 7") != std::string::npos && entries[0].summary.find("+Lantern") != std::string::npos &&
              entries[0].before["hp"] == 13 && entries[0].after["hp"] == 7 && !entries[0].undone,
          "the change is logged with what it was before: " + (entries.empty() ? std::string() : entries[0].summary));
    const auto fresh = log.poll();
    check(fresh.size() == 1 && fresh[0].first == "c-test" && fresh[0].second.id == entries[0].id && log.poll().empty(), "the app hears about a new entry once");

    check(sheet::editFile(path, "c-test", parse(R"({"hp": 7})"), true, &log).keys.empty() && log.entries("c-test").size() == 1 && sheet::editFile(path, "c-test", parse(R"({"hp": 7})"), true, &log).character.revision == 5,
          "a no-op edit is not logged and does not touch the revision");
    check(sheet::editFile(path, "c-test", parse(R"({"hp": "ten"})"), true, &log).status == 400 && sheet::editFile(path, "c-test", parse(R"({"kin": {}})"), true, &log).status == 400, "bad values and forbidden fields are 400");
    check(sheet::editFile(root + "nobody.json", "nobody", parse(R"({"hp": 1})"), true, &log).status == 404, "a character that does not exist is 404");

    json many = json::array();
    for (int i = 0; i < 12; ++i) many.push_back({{"name", "Stone " + std::to_string(i)}});
    json setMany = json::object();
    setMany["inventory"] = many;
    r = sheet::editFile(path, "c-test", setMany, true, &log);
    check(r.ok && !sheet::openIssues(r.character).empty() && sheet::openIssues(r.character)[0].key == "encumbrance", "carrying too much is allowed, and flagged");
    check(sheet::review(r.character, "encumbrance", true), "the GM approves it");
    Character approved = r.character;
    check(sheet::openIssues(approved).empty(), "it is normal now");
    json ruling = json::object();
    ruling["reviews"] = toDoc(approved)["reviews"];
    check(sheet::editFile(path, "c-test", ruling, false, &log).ok, "the GM's own path writes the ruling");
    r = sheet::editFile(path, "c-test", parse(R"({"notes": "still carrying it"})"), true, &log);
    check(r.ok && r.character.reviews.contains("encumbrance") && sheet::openIssues(r.character).empty(), "the ruling survives edits that do not touch what it judged");
    r = sheet::editFile(path, "c-test", parse(R"({"inventory": [{"name": "Torch"}]})"), true, &log);
    check(r.ok && r.character.reviews.empty(), "and is forgotten once the player fixes the problem");

    Character lockedC = r.character;
    lockedC.locked = true;
    test::write(path, lockedC.toJson());
    check(sheet::editFile(path, "c-test", parse(R"({"hp": 1})"), true, &log).status == 423 && sheet::editFile(path, "c-test", parse(R"({"hp": 1})"), false, &log).ok, "a locked sheet refuses the player, not the GM");

    // undo, the way the app does it
    const auto all = log.entries("c-test");
    json doc2 = parse(fs::readFile(path).value_or(""));
    doc2.erase("locked");
    sheet::merge(doc2, all[0].before);
    check(doc2["hp"] == 13 && doc2["inventory"].size() == 2 && log.markUndone("c-test", all[0].id) && log.entries("c-test")[0].undone && !log.markUndone("c-test", all[0].id), "the log keeps what to put back, and marks an entry undone once");

    // ------------------------------------------------------------ two writers, one file
    const std::string dir = root + "store";
    CharacterStore app;
    app.setDir(dir);
    Character start = sample();
    start.id.clear();
    check(app.save(start) && start.revision == 1, "a new character starts at revision 1");
    const std::string id = start.id;
    CharacterStore watcher;
    watcher.setDir(dir);
    check(watcher.poll().empty(), "a store that has just loaded has nothing new to report");

    sheet::EditResult web = sheet::editFile(dir + "/" + id + ".json", id, parse(R"({"hp": 4, "coins": {"gold": 9}})"), true, &log);   // the web server, meanwhile
    Character mine = *app.find(id);                                   // the app's copy is stale: it has not looked yet
    mine.notes = "Met the captain.";
    mine.attr[2] = 14;
    check(app.save(mine), "the app saves an edit made on a stale copy");
    Character now2;
    Character::fromJson(fs::readFile(dir + "/" + id + ".json").value_or(""), now2, nullptr);
    check(now2.hp == 4 && now2.gold == 9 && now2.notes == "Met the captain." && now2.attr[2] == 14 && now2.revision == 3 && web.ok,
          "the file holds both people's changes: the player's HP and coins, the GM's notes and attribute");
    check(mine.hp == 4 && app.find(id)->hp == 4, "and the app's copy now shows them too");

    const std::vector<std::string> changed = watcher.poll();
    check(changed == std::vector<std::string>{id} && watcher.find(id)->hp == 4 && watcher.find(id)->notes == "Met the captain.", "another store notices the file changed and reloads it");
    check(app.poll().empty(), "a store does not report its own writes as somebody else's");
    web = sheet::editFile(dir + "/" + id + ".json", id, parse(R"({"hp": 2})"), true, nullptr);
    check(app.poll() == std::vector<std::string>{id} && app.find(id)->hp == 2, "but it does see the other program's next one");
    Character gone = *watcher.find(id);
    watcher.remove(id);
    check(app.poll() == std::vector<std::string>{id} && app.find(id) == nullptr, "and a deleted file");

    return test::finish();
}
