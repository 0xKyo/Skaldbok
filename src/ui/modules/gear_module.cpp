// Gear: weapons, armor and everyday equipment. The book has no separate "rules" text for these beyond the tables
// themselves (Weapons & Armor terms, Melee/Ranged Weapons, Armor & Helmets, Trade Goods...), so there is no list+detail
// here - just the one reading page, gear.json's own intro, with the books' tables inline.
#include <imgui.h>

#include "ui/modules/modules.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

class GearModule : public Module {
public:
    explicit GearModule(Host& host) : Module(host) {}

    const char* id() const override { return "gear"; }
    const char* title() const override { return "Gear"; }
    const char* group() const override { return "Reference"; }
    Layout layout() const override { return Layout::Full; }
    int badge() const override { return host_.content().count(Kind::Weapon) + host_.content().count(Kind::Armor) + host_.content().count(Kind::Gear); }
    bool handles(Kind k, int) const override { return k == Kind::Weapon || k == Kind::Armor || k == Kind::Gear; }

    bool findByName(const std::string& want, Selection& out) const override {
        for (Kind k : {Kind::Weapon, Kind::Armor, Kind::Gear})
            if (const Entry* e = host_.content().findByName(k, want)) {
                out = {k, e->id};
                return true;
            }
        return false;
    }

    // A specific weapon/armor/gear item has no page of its own to jump to: the whole chapter is the one page.
    void onSelect(const Selection&) override {}

    void drawFull() override {
        const Intro& intro = host_.content().introOf(Kind::Gear);
        if (intro.empty()) ImGui::TextColored(kGrey, "Nothing here yet.");
        else introPage(intro, "##gear");
    }
};

}  // namespace

std::unique_ptr<Module> makeGearModule(Host& host) { return std::make_unique<GearModule>(host); }

}  // namespace gm
