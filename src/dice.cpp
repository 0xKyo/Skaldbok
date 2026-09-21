#include "dice.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <random>

namespace gm {

std::string DiceExpr::text() const {
    std::string s = (count == 1 ? "D" : std::to_string(count) + "D") + std::to_string(sides);
    if (multiplier != 1) s += "×" + std::to_string(multiplier);
    if (modifier > 0) s += "+" + std::to_string(modifier);
    if (modifier < 0) s += std::to_string(modifier);
    return s;
}

std::optional<DiceExpr> parseDice(const std::string& text) {
    size_t i = 0;
    const size_t n = text.size();
    auto skip = [&] { while (i < n && text[i] == ' ') ++i; };
    auto number = [&](int& out) {
        if (i >= n || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        long v = 0;
        while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) v = v * 10 + (text[i++] - '0');
        out = static_cast<int>(v > 1000000 ? 1000000 : v);
        return true;
    };
    DiceExpr e;
    skip();
    int c = 1;
    if (number(c)) e.count = c;
    if (i >= n || (text[i] != 'd' && text[i] != 'D')) return std::nullopt;
    ++i;
    if (!number(e.sides) || e.sides < 1 || e.count < 1 || e.count > 100) return std::nullopt;
    skip();
    for (;;) {
        if (i >= n) break;
        const char op = text[i];
        if (op == '+' || op == '-') {
            ++i;
            skip();
            int v = 0;
            if (!number(v)) return std::nullopt;
            e.modifier += op == '+' ? v : -v;
        } else if (op == '*' || op == 'x' || op == 'X' || static_cast<unsigned char>(op) == 0xC3) {
            // '*', 'x' or the UTF-8 multiplication sign
            i += static_cast<unsigned char>(op) == 0xC3 ? 2 : 1;
            skip();
            int v = 0;
            if (!number(v)) return std::nullopt;
            e.multiplier *= v;
        } else {
            return std::nullopt;
        }
        skip();
    }
    return e;
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

const char* Dice::checkOutcome(int d20, int skillLevel) {
    if (d20 == 1) return "Dragon (critical success)";
    if (d20 == 20) return "Demon (critical failure)";
    return d20 <= skillLevel ? "Success" : "Failure";
}

}  // namespace gm
