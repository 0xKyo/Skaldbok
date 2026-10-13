#include "game/dice.h"

#include <chrono>
#include <random>

namespace gm {

std::string DiceExpr::text() const {
    std::string s = (count == 1 ? "D" : std::to_string(count) + "D") + std::to_string(sides);
    if (multiplier != 1) s += "×" + std::to_string(multiplier);
    if (modifier > 0) s += "+" + std::to_string(modifier);
    if (modifier < 0) s += std::to_string(modifier);
    return s;
}

std::string DiceRoll::describe() const {
    std::string s = expr.text() + " → ";
    if (dice.size() > 1 || expr.modifier != 0 || expr.multiplier != 1) {
        s += "[";
        for (size_t i = 0; i < dice.size(); ++i) s += (i ? "," : "") + std::to_string(dice[i]);
        s += "] = ";
    }
    return s + std::to_string(total);
}

Dice::Dice() {
    std::random_device rd;
    state_ = (static_cast<unsigned long long>(rd()) << 32) ^ rd() ^
             static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (state_ == 0) state_ = 0x9E3779B97F4A7C15ull;
}

unsigned long long Dice::next() {   // xorshift64*: plenty for tabletop dice
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 0x2545F4914F6CDD1Dull;
}

int Dice::roll(int sides) {
    if (sides < 1) return 0;
    return static_cast<int>(next() % static_cast<unsigned long long>(sides)) + 1;
}

DiceRoll Dice::roll(const DiceExpr& e, const std::string& label) {
    DiceRoll r;
    r.expr = e;
    r.label = label;
    int sum = 0;
    for (int i = 0; i < e.count; ++i) {
        r.dice.push_back(roll(e.sides));
        sum += r.dice.back();
    }
    r.total = sum * e.multiplier + e.modifier;
    return r;
}

}  // namespace gm
