#include "game/encounter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <numeric>
#include <sstream>

namespace gm {

const ConditionInfo kConditions[6] = {
    {"Exhausted", "STR"}, {"Angry", "INT"}, {"Sickly", "CON"},
    {"Scared", "WIL"},    {"Dazed", "AGL"}, {"Disheartened", "CHA"},
};

int Encounter::add(Combatant c) {
    c.hp = std::max(0, c.hp);
    c.maxHp = std::max(c.maxHp, c.hp);
    c.uid = nextUid_++;
    combatants.push_back(std::move(c));
    return static_cast<int>(combatants.size()) - 1;
}

int Encounter::indexOfUid(int uid) const {
    for (size_t i = 0; i < combatants.size(); ++i)
        if (combatants[i].uid == uid) return static_cast<int>(i);
    return -1;
}

void Encounter::remove(int index) {
    if (index < 0 || index >= static_cast<int>(combatants.size())) return;
    combatants.erase(combatants.begin() + index);
    if (active > index) --active;
    else if (active == index) active = combatants.empty() ? -1 : std::min(active, static_cast<int>(combatants.size()) - 1);
}

void Encounter::clear() {
    combatants.clear();
    round = 1;
    active = -1;
}

void Encounter::changeHp(int index, int delta) {
    if (index < 0 || index >= static_cast<int>(combatants.size())) return;
    Combatant& c = combatants[static_cast<size_t>(index)];
    c.hp = std::clamp(c.hp + delta, 0, std::max(c.maxHp, c.hp));
}

void Encounter::drawInitiative(Dice& dice) {
    const size_t n = combatants.size();
    if (n == 0) return;
    // Shuffle the ten cards; with more than ten combatants the deck is reshuffled for the rest.
    std::vector<int> deck;
    for (size_t i = 0; i < n; ++i) {
        if (deck.empty()) {
            std::vector<int> cards(10);
            std::iota(cards.begin(), cards.end(), 1);
            for (int k = 9; k > 0; --k) std::swap(cards[static_cast<size_t>(k)], cards[static_cast<size_t>(dice.roll(k + 1) - 1)]);
            deck = cards;
        }
        combatants[i].initiative = deck.back();
        deck.pop_back();
    }
}

void Encounter::sortByInitiative() {
    std::vector<size_t> order(combatants.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, [&](size_t a, size_t b) {
        const int ia = combatants[a].initiative == 0 ? 99 : combatants[a].initiative;
        const int ib = combatants[b].initiative == 0 ? 99 : combatants[b].initiative;
        return ia < ib;
    });
    std::vector<Combatant> sorted;
    sorted.reserve(combatants.size());
    int newActive = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        if (static_cast<int>(order[i]) == active) newActive = static_cast<int>(i);
        sorted.push_back(std::move(combatants[order[i]]));
    }
    combatants = std::move(sorted);
    active = newActive;
}

void Encounter::start() {
    sortByInitiative();
    round = 1;
    active = -1;
    for (size_t i = 0; i < combatants.size(); ++i)
        if (combatants[i].alive()) {
            active = static_cast<int>(i);
            break;
        }
}

void Encounter::nextTurn() {
    if (combatants.empty()) return;
    if (active < 0) {
        start();
        return;
    }
    // next living combatant; passing the end of the list starts a new round
    const int n = static_cast<int>(combatants.size());
    for (int step = 1; step <= n; ++step) {
        const int raw = active + step;
        const int idx = raw % n;
        if (combatants[static_cast<size_t>(idx)].alive()) {
            if (raw >= n) ++round;            // went past the end of the list: a new round
            active = idx;
            return;
        }
    }
}

// Format: "R<TAB>round<TAB>active" then one
// "C<TAB>monsterKey<TAB>hp<TAB>maxHp<TAB>init<TAB>conditions<TAB>name<TAB>note<TAB>characterId" per line.
// (Older files held a numeric creature id in the second column; those creatures are simply not linked any more.)
std::string Encounter::serialize() const {
    auto clean = [](std::string s) {
        for (char& c : s)
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        return s;
    };
    std::ostringstream out;
    out << "R\t" << round << '\t' << active << '\n';
    for (const Combatant& c : combatants)
        out << "C\t" << clean(c.monsterKey) << '\t' << c.hp << '\t' << c.maxHp << '\t' << c.initiative << '\t' << c.conditions
            << '\t' << clean(c.name) << '\t' << clean(c.note) << '\t' << clean(c.characterId) << '\n';
    return out.str();
}

Encounter Encounter::parse(const std::string& text) {
    Encounter e;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        size_t pos = 0;
        while (true) {
            const size_t t = line.find('\t', pos);
            f.push_back(line.substr(pos, t == std::string::npos ? std::string::npos : t - pos));
            if (t == std::string::npos) break;
            pos = t + 1;
        }
        if (f[0] == "R" && f.size() >= 3) {
            e.round = std::max(1, std::atoi(f[1].c_str()));
            e.active = std::atoi(f[2].c_str());
        } else if (f[0] == "C" && f.size() >= 7) {
            Combatant c;
            const bool legacyId = !f[1].empty() && std::ranges::all_of(f[1], [](unsigned char ch) { return std::isdigit(ch); });
            c.monsterKey = legacyId ? std::string() : f[1];
            c.hp = std::max(0, std::atoi(f[2].c_str()));
            c.maxHp = std::max(c.hp, std::atoi(f[3].c_str()));
            c.initiative = std::clamp(std::atoi(f[4].c_str()), 0, 99);
            c.conditions = static_cast<unsigned>(std::strtoul(f[5].c_str(), nullptr, 10)) & 0x3Fu;
            c.name = f[6];
            c.note = f.size() > 7 ? f[7] : std::string();
            c.characterId = f.size() > 8 ? f[8] : std::string();
            e.add(std::move(c));
        }
    }
    if (e.active >= static_cast<int>(e.combatants.size())) e.active = -1;
    return e;
}

int Encounter::firstNumber(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i)
        if (std::isdigit(static_cast<unsigned char>(text[i]))) return std::atoi(text.c_str() + i);
    return 0;
}

}  // namespace gm
