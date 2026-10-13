#pragma once

#include <string>
#include <vector>

namespace gm {

// A dice expression: "2D8+3", "D6", "D20-1", "3D6×10".
struct DiceExpr {
    int count = 1, sides = 6, modifier = 0, multiplier = 1;
    std::string text() const;
};

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

private:
    unsigned long long state_;
    unsigned long long next();
};

}  // namespace gm
