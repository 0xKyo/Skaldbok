#include <cstdio>
#include <string>

#include "encounter.h"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (!ok) ++failures;
}
gm::Combatant make(const std::string& name, int hp, int init = 0) {
    gm::Combatant c;
    c.name = name;
    c.hp = c.maxHp = hp;
    c.initiative = init;
    return c;
}
}  // namespace

int main() {
    using gm::Encounter;
    Encounter e;
    e.add(make("Goblin", 9, 7));
    e.add(make("Aria", 14, 2));
    e.add(make("Troll", 40, 5));
    e.add(make("Undrawn", 5));

    e.start();
    check(e.combatants[0].name == "Aria" && e.combatants[1].name == "Troll" && e.combatants[2].name == "Goblin" &&
              e.combatants[3].name == "Undrawn",
          "start sorts by initiative card, undrawn last");
    check(e.active == 0 && e.round == 1, "first combatant acts in round 1");

    e.nextTurn();
    e.nextTurn();
    e.nextTurn();
    check(e.active == 3 && e.round == 1, "turn order walks the list");
    e.nextTurn();
    check(e.active == 0 && e.round == 2, "after the last combatant a new round begins");

    e.changeHp(1, -100);
    check(e.combatants[1].hp == 0, "hp never drops below zero");
    e.changeHp(1, +100);
    check(e.combatants[1].hp == 40, "healing stops at max hp");
    e.changeHp(1, -100);
    e.nextTurn();
    check(e.active == 2, "downed combatants are skipped");

    // removing keeps the active marker on the same combatant
    e.remove(0);
    check(e.combatants.size() == 3 && e.combatants[static_cast<size_t>(e.active)].name == "Goblin",
          "removing an earlier combatant keeps the active one");

    // save / load
    e.combatants[0].conditions = 0b101;
    e.combatants[0].note = "has\ta\nweird note";
    const Encounter back = Encounter::parse(e.serialize());
    check(back.combatants.size() == e.combatants.size() && back.round == e.round && back.active == e.active,
          "serialize/parse keeps round, active and combatants");
    check(back.combatants[0].conditions == 0b101 && back.combatants[0].note == "has a weird note",
          "conditions survive, control characters in notes are flattened");
    check(Encounter::parse("garbage\nC\t1\n").combatants.empty(), "malformed input is ignored");

    // creatures are saved by their stable content key (numeric handles change when packs do)
    e.combatants[0].monsterKey = "core/monster/bestiary-goblin";
    e.combatants[0].characterId = "c-1";
    e.combatants[1].monsterId = 7;                          // a runtime handle: never saved
    const Encounter keyed = Encounter::parse(e.serialize());
    check(keyed.combatants[0].monsterKey == "core/monster/bestiary-goblin" && keyed.combatants[0].characterId == "c-1" &&
              keyed.combatants[1].monsterKey.empty() && keyed.combatants[1].monsterId == 0,
          "creature key and character id survive; handles are not saved");
    check(Encounter::parse("R\t2\t0\nC\t42\t5\t9\t3\t0\tOld save\t\n").combatants[0].monsterKey.empty(),
          "an old save with a numeric creature id still loads (unlinked)");

    // initiative cards
    gm::Dice dice;
    Encounter big;
    for (int i = 0; i < 10; ++i) big.add(make("c" + std::to_string(i), 5));
    big.drawInitiative(dice);
    int seen = 0;
    for (const auto& c : big.combatants) seen |= 1 << c.initiative;
    check(seen == 0b11111111110, "ten combatants draw ten different cards 1..10");
    big.add(make("eleventh", 5));
    big.drawInitiative(dice);
    bool inRange = true;
    for (const auto& c : big.combatants) inRange &= c.initiative >= 1 && c.initiative <= 10;
    check(inRange, "more than ten combatants still draw cards in range");

    check(Encounter::firstNumber("28") == 28 && Encounter::firstNumber("HP 2D6") == 2 && Encounter::firstNumber("—") == 0,
          "firstNumber reads HP out of stat block text");

    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
