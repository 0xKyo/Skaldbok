#pragma once

#include <optional>
#include <string>
#include <vector>

namespace gm {

// "2D8+3", "d6", "D20-1", "3D6*10"; case-insensitive. Returns nullopt if the text is not a dice expression.
struct DiceExpr {
    int count = 1, sides = 6, modifier = 0, multiplier = 1;
    std::string text() const;
};
std::optional<DiceExpr> parseDice(const std::string& text);

struct DiceRoll {
    DiceExpr expr;
    std::vector<int> dice;
    int total = 0;
    std::string label;      // what it was for ("Centaur attack", "free roll")
    std::string describe() const;
};

class Dice {
public:
    Dice();
    int roll(int sides);                       // 1..sides
    DiceRoll roll(const DiceExpr& e, const std::string& label = {});
    // Dragonbane check: roll a D20 against a skill level (dragon = 1, demon = 20).
    static const char* checkOutcome(int d20, int skillLevel);

private:
    unsigned long long state_;
    unsigned long long next();
};

}  // namespace gm
