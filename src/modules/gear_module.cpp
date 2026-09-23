// Gear: weapons, armor and everyday equipment. The book has no separate "rules" text for these beyond the tables
// themselves (Weapons & Armor terms, Melee/Ranged Weapons, Armor & Helmets, Trade Goods...), so there is no list+detail
// here, no mosaic either - just the one reading page, gear.json's own intro, with the books' tables inline.
#include <imgui.h>

#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

class GearModule : public Module {
public:
    explicit GearModule(Host& host) : Module(host) {}

    const char* id() const override { return "gear"; }
    const char* title() const override { return "Gear"; }
    const char* summary() const override { return "Weapons, armor and equipment: the books' own tables, and what each column means."; }
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
        ImGui::BeginChild("##gear", ImVec2(0, 0));
        if (intro.empty()) {
            ImGui::TextColored(kGrey, "Nothing here yet.");
            ImGui::EndChild();
            return;
        }
        if (!intro.body.empty()) paragraphs(intro.body, "introbody");
        for (size_t i = 0; i < intro.sections.size(); ++i) {
            const RuleNode::Section& sec = intro.sections[i];
            ImGui::Spacing();
            bigText(sec.title.c_str(), 1.1f, kGold);
            paragraphs(sec.body, ("introsec" + std::to_string(i)).c_str());
        }
        for (size_t i = 0; i < intro.tables.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Spacing();
            bigText(intro.tables[i].title.c_str(), 1.1f, kAccent);
            tableGrid(intro.tables[i], -1);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
};

}  // namespace

std::unique_ptr<Module> makeGearModule(Host& host) { return std::make_unique<GearModule>(host); }

}  // namespace gm
