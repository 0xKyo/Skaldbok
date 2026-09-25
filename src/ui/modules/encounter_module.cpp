// The combat tracker: turn order, hit points, conditions, and rolling a creature's attack.
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "game/encounter.h"
#include "parsing/fsutil.h"
#include "ui/modules/modules.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

std::string hpOf(const StatBlock& b) {
    for (const Field& f : b.fields)
        if (lowered(f.label) == "hp") return f.value;
    return {};
}

class EncounterModule : public Module, public IEncounterSink {
public:
    explicit EncounterModule(Host& host) : Module(host) {
        file_ = host_.paths().prefDir + "encounter.txt";
        if (const auto text = fs::readFile(file_)) enc_ = Encounter::parse(*text);
        resolveCreatures();
    }

    const char* id() const override { return "encounter"; }
    const char* title() const override { return "Encounter"; }
    const char* group() const override { return "Play"; }
    bool ownsDetail() const override { return true; }
    int badge() const override { return static_cast<int>(enc_.combatants.size()); }

    void onContentChanged() override { resolveCreatures(); }

    // ---- IEncounterSink ---------------------------------------------------------------------------
    void addCreature(const Monster& m, const StatBlock* block) override {
        Combatant c;
        c.monsterId = m.id;
        c.monsterKey = m.key;
        c.name = m.name + (block && !block->variant.empty() ? " (" + block->variant + ")" : "");
        if (block) c.hp = c.maxHp = Encounter::firstNumber(hpOf(*block));
        // several of the same kind get numbered so they can be told apart
        int same = 0;
        for (const Combatant& o : enc_.combatants)
            if (o.name == c.name || o.name.starts_with(c.name + " #")) ++same;
        if (same > 0) {
            if (same == 1)
                for (Combatant& o : enc_.combatants)
                    if (o.name == c.name) o.name += " #1";
            c.name += " #" + std::to_string(same + 1);
        }
        const int idx = enc_.add(std::move(c));
        selUid_ = enc_.combatants[static_cast<size_t>(idx)].uid;
        save();
        host_.notify("Added " + enc_.combatants[static_cast<size_t>(idx)].name + " to the encounter");
    }

    void addPlayer(const Character& ch) override {
        for (const Combatant& o : enc_.combatants)
            if (o.characterId == ch.id) {
                host_.notify(ch.name + " is already in the encounter");
                return;
            }
        Combatant c;
        c.characterId = ch.id;
        c.name = ch.name.empty() ? "(unnamed)" : ch.name;
        c.hp = ch.hp;
        c.maxHp = maxHp(ch);
        c.conditions = ch.conditions;
        selUid_ = enc_.combatants[static_cast<size_t>(enc_.add(std::move(c)))].uid;
        save();
        host_.notify("Added " + ch.name + " to the encounter");
    }

    void demoFight(bool selectAttackRoll) override {
        enc_.clear();
        for (const char* who : {"Goblin", "The Worg Rider", "Centaur"}) {
            for (const Monster& m : host_.content().monsters()) {
                if (m.name == who && !m.blocks.empty()) {
                    addCreature(m, &m.blocks.front());
                    if (std::string(who) == "Goblin") addCreature(m, &m.blocks.front());
                    break;
                }
            }
        }
        for (const char* pc : {"Aria (player)", "Bjorn (player)"}) {
            Combatant c;
            c.name = pc;
            c.hp = c.maxHp = 13;
            enc_.add(std::move(c));
        }
        enc_.drawInitiative(host_.dice());
        enc_.start();
        enc_.changeHp(enc_.active >= 0 ? enc_.active : 0, -6);
        if (!enc_.combatants.empty()) {
            enc_.combatants[1 % enc_.combatants.size()].conditions = 0b100001;
            selUid_ = enc_.combatants[static_cast<size_t>(std::max(0, enc_.active))].uid;
        }
        for (const Combatant& c : enc_.combatants)        // show a creature that has an attack table
            if (c.name == "The Worg Rider") selUid_ = c.uid;
        if (selectAttackRoll) {
            attackRoll_ = 4;
            attackRow_ = 3;
        }
    }

    // ---- the panes ---------------------------------------------------------------------------------
    void drawList() override {
        ImGui::TextColored(kAccent, "Round %d", enc_.round);
        ImGui::SameLine();
        if (enc_.active >= 0 && enc_.active < static_cast<int>(enc_.combatants.size()))
            ImGui::TextColored(kGrey, "· %s acts", enc_.combatants[static_cast<size_t>(enc_.active)].name.c_str());

        if (ImGui::Button("Draw cards")) {
            enc_.drawInitiative(host_.dice());
            enc_.active = -1;
            enc_.sortByInitiative();
            save();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everyone draws an initiative card 1-10 (lower acts first)");
        ImGui::SameLine();
        ImGui::BeginDisabled(enc_.combatants.empty());
        if (ImGui::Button(enc_.active < 0 ? "Start combat" : "Next turn")) {
            if (enc_.active < 0) enc_.start();
            else enc_.nextTurn();
            if (enc_.active >= 0) selUid_ = enc_.combatants[static_cast<size_t>(enc_.active)].uid;
            save();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) confirmClear_ = true;

        if (confirmClear_) {
            ImGui::OpenPopup("Clear the encounter?");
            confirmClear_ = false;
        }
        if (ImGui::BeginPopupModal("Clear the encounter?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Remove every combatant and reset the round counter?");
            if (ImGui::Button("Clear")) {
                enc_.clear();
                selUid_ = 0;
                save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        const float addH = ImGui::GetFrameHeightWithSpacing() * 3.2f;
        ImGui::BeginChild("##combatants", ImVec2(0, -addH));
        if (enc_.combatants.empty())
            ImGui::TextWrapped("Empty. Use \"Add to encounter\" on any creature or character, or add a name below.");
        for (size_t i = 0; i < enc_.combatants.size(); ++i) {
            Combatant& c = enc_.combatants[i];
            const bool selected = c.uid == selUid_;
            const bool isActive = static_cast<int>(i) == enc_.active;
            const float rowH = lineH() * 2.0f + 12.0f;
            ImGui::PushID(c.uid);
            if (ImGui::Selectable("##c", selected, 0, ImVec2(0, rowH))) selUid_ = c.uid;
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (isActive) dl->AddRectFilled(mn, ImVec2(mn.x + 4, mx.y), ImGui::GetColorU32(kGold), 2.0f);

            char card[8];
            if (c.initiative > 0) std::snprintf(card, sizeof card, "%d", c.initiative);
            else std::snprintf(card, sizeof card, "-");
            const ImVec2 cs = ImGui::CalcTextSize(card);
            dl->AddRectFilled(ImVec2(mn.x + 10, mn.y + 8), ImVec2(mn.x + 10 + U(30), mn.y + 8 + U(30)), ImGui::GetColorU32(ImVec4(0.733f, 0.847f, 0.800f, 1.0f)),
                              5.0f);
            dl->AddText(ImVec2(mn.x + 10 + (U(30) - cs.x) * 0.5f, mn.y + 8 + (U(30) - cs.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), card);

            const ImU32 nameCol = c.alive() ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(kGrey);
            dl->AddText(ImVec2(mn.x + U(50), mn.y + 6), nameCol, c.name.c_str());
            // conditions as their attribute tags
            std::string tags;
            for (int b = 0; b < 6; ++b)
                if (c.conditions & (1u << b)) tags += std::string(tags.empty() ? "" : " ") + kConditions[b].attribute;
            if (!tags.empty()) {
                const ImVec2 ts = ImGui::CalcTextSize(tags.c_str());
                dl->AddText(ImVec2(mx.x - ts.x - 8, mn.y + 6), ImGui::GetColorU32(kRed), tags.c_str());
            }
            // hit point bar
            const float barL = mn.x + U(50), barR = mx.x - 10, barY = mn.y + 6 + lineH() + 6;
            const float frac = c.maxHp > 0 ? static_cast<float>(c.hp) / static_cast<float>(c.maxHp) : 0.0f;
            dl->AddRectFilled(ImVec2(barL, barY), ImVec2(barR, barY + 8), ImGui::GetColorU32(ImVec4(0.82f, 0.75f, 0.58f, 1)), 4.0f);
            if (frac > 0)
                dl->AddRectFilled(ImVec2(barL, barY), ImVec2(barL + (barR - barL) * std::min(1.0f, frac), barY + 8),
                                  ImGui::GetColorU32(frac > 0.5f ? kAccent : (frac > 0.25f ? kGold : kRed)), 4.0f);
            char hp[32];
            std::snprintf(hp, sizeof hp, "%d / %d", c.hp, c.maxHp);
            const ImVec2 hs = ImGui::CalcTextSize(hp);
            dl->AddText(ImVec2(mx.x - hs.x - 8, barY + 10), ImGui::GetColorU32(kGrey), hp);
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::SetNextItemWidth(U(140));
        ImGui::InputTextWithHint("##newname", "Name (player, NPC...)", newName_, sizeof newName_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(U(70));
        ImGui::InputInt("HP", &newHp_, 0, 0);
        newHp_ = std::clamp(newHp_, 1, 999);
        if (ImGui::Button("Add combatant") && newName_[0]) {
            Combatant c;
            c.name = newName_;
            c.hp = c.maxHp = newHp_;
            selUid_ = enc_.combatants[static_cast<size_t>(enc_.add(std::move(c)))].uid;
            newName_[0] = 0;
            save();
        }
    }

    void drawDetail() override {
        const int idx = enc_.indexOfUid(selUid_);
        if (idx < 0) {
            ImGui::Spacing();
            bigText("Combat tracker", 1.5f, kAccent);
            ImGui::TextWrapped("Add creatures from their page with the \"Add to encounter\" button, add your characters from the Characters "
                               "module, or add names below the list. Then Draw cards to give everyone an initiative card (1 to 10, lower "
                               "acts first), Start combat and use Next turn. Hit points, conditions and notes are saved automatically.");
            return;
        }
        Combatant& c = enc_.combatants[static_cast<size_t>(idx)];
        bool changed = false;

        char name[96];
        std::snprintf(name, sizeof name, "%s", c.name.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##name", name, sizeof name)) {
            c.name = name;
            changed = true;
        }

        // hit points
        ImGui::TextColored(kGold, "Hit points");
        ImGui::SameLine();
        ImGui::TextColored(c.alive() ? kGrey : kRed, c.alive() ? "" : "  at 0 HP: death roll or rally (Combat chapter)");
        const int deltas[] = {-5, -1, 1, 5};
        for (int d : deltas) {
            char label[8];
            std::snprintf(label, sizeof label, "%+d", d);
            ImGui::PushID(d);
            if (ImGui::Button(label, ImVec2(U(46), 0))) {
                enc_.changeHp(idx, d);
                changed = true;
            }
            ImGui::PopID();
            ImGui::SameLine(0, 4);
        }
        ImGui::SetNextItemWidth(U(110));
        if (ImGui::InputInt("##hp", &c.hp, 0, 0)) {
            c.hp = std::clamp(c.hp, 0, std::max(c.maxHp, 999));
            c.maxHp = std::max(c.maxHp, c.hp);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "of");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(U(110));
        if (ImGui::InputInt("##maxhp", &c.maxHp, 0, 0)) {
            c.maxHp = std::clamp(c.maxHp, 1, 999);
            c.hp = std::min(c.hp, c.maxHp);
            changed = true;
        }

        // initiative
        ImGui::TextColored(kGold, "Initiative card");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(U(110));
        if (ImGui::InputInt("##init", &c.initiative, 1, 1)) {
            c.initiative = std::clamp(c.initiative, 0, 10);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Draw")) {
            c.initiative = host_.dice().roll(10);
            changed = true;
        }

        // conditions: each gives a bane on its attribute and the skills based on it
        ImGui::Spacing();
        ImGui::TextColored(kGold, "Conditions");
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "(a bane on that attribute and its skills)");
        for (int b = 0; b < 6; ++b) {
            bool on = c.conditions & (1u << b);
            char label[48];
            std::snprintf(label, sizeof label, "%s (%s)", kConditions[b].name, kConditions[b].attribute);
            if (ImGui::Checkbox(label, &on)) {
                if (on) c.conditions |= 1u << b;
                else c.conditions &= ~(1u << b);
                changed = true;
            }
            if (b % 3 != 2) ImGui::SameLine(0, 16);
        }

        char note[200];
        std::snprintf(note, sizeof note, "%s", c.note.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##note", "Notes (tactics, loot, what it just did...)", note, sizeof note)) {
            c.note = note;
            changed = true;
        }

        ImGui::Spacing();
        if (c.monsterId != 0 && ImGui::Button("Open creature")) host_.goTo(Kind::Monster, c.monsterId);
        if (c.monsterId != 0) ImGui::SameLine();
        if (!c.characterId.empty())
            if (ICharacters* chars = serviceOf<ICharacters>(host_)) {
                if (ImGui::Button("Open character")) chars->openCharacter(c.characterId);
                ImGui::SameLine();
            }
        if (ImGui::Button("Remove from encounter")) {
            enc_.remove(idx);
            selUid_ = 0;
            save();
            return;
        }
        if (changed) {
            save();
            syncCharacter(c);
        }

        // the creature's own stat block and attack table, right where the GM needs them
        if (const Monster* m = c.monsterId ? host_.content().monster(c.monsterId) : nullptr) {
            if (attackFor_ != c.monsterId) {
                attackFor_ = c.monsterId;
                attackRow_ = -1;
            }
            ImGui::Separator();
            if (!m->blocks.empty() && ImGui::BeginTable("##encstat", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                const StatBlock& b = m->blocks.front();
                for (size_t i = 0; i < b.fields.size(); ++i) {
                    if (i % 2 == 0) ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(static_cast<int>(i % 2) * 2);
                    ImGui::TextColored(kGold, "%s", b.fields[i].label.c_str());
                    ImGui::TableSetColumnIndex(static_cast<int>(i % 2) * 2 + 1);
                    ImGui::TextWrapped("%s", b.fields[i].value.c_str());
                }
                ImGui::EndTable();
            }
            attacksTable(host_, *m, attackRow_, attackRoll_);
        }
    }

private:
    void save() const {
        fs::writeFile(file_, enc_.serialize());
    }

    // Saved fights name their creatures by key; find them again in whatever is loaded now.
    void resolveCreatures() {
        for (Combatant& c : enc_.combatants) c.monsterId = c.monsterKey.empty() ? 0 : host_.content().idByKey(Kind::Monster, c.monsterKey);
    }

    // A player character in the fight keeps its hit points and conditions in step with its sheet.
    void syncCharacter(const Combatant& c) {
        if (c.characterId.empty()) return;
        Character* ch = host_.characters().find(c.characterId);
        if (!ch || (ch->hp == c.hp && ch->conditions == c.conditions)) return;
        Character copy = *ch;
        copy.hp = std::clamp(c.hp, 0, 99);
        copy.conditions = c.conditions;
        host_.characters().save(copy);
    }

    Encounter enc_;
    std::string file_;
    int selUid_ = 0;                                     // selected combatant, by uid
    int attackFor_ = 0;
    int attackRow_ = -1, attackRoll_ = 0;
    char newName_[64] = {};
    int newHp_ = 12;
    bool confirmClear_ = false;
};

}  // namespace

std::unique_ptr<Module> makeEncounterModule(Host& host) { return std::make_unique<EncounterModule>(host); }

}  // namespace gm
