// Party: a group of characters that play together. A party is a JSON file (docs/CHARACTERS.md). It needs no other module:
// the encounter, message and Master Screen buttons appear only when those modules are on.
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "ui/modules/modules.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

class PartyModule : public Module {
public:
    explicit PartyModule(Host& host) : Module(host) {}

    const char* id() const override { return "party"; }
    const char* title() const override { return "Party"; }
    const char* group() const override { return "Play"; }
    bool ownsDetail() const override { return true; }
    int badge() const override { return static_cast<int>(host_.parties().all().size()); }

    void drawList() override {
        PartyStore& store = host_.parties();
        if (ImGui::Button("New party")) {
            Party p;
            p.name = "New party";
            if (store.save(p)) selId_ = p.id;
        }
        ImGui::BeginChild("##parties", ImVec2(0, 0));
        if (store.all().empty()) ImGui::TextWrapped("No parties yet. A party groups the characters that play together.");
        for (const Party& p : store.all()) {
            const float rowH = lineH() * 2.0f + 12.0f;
            ImGui::PushID(p.id.c_str());
            if (ImGui::Selectable("##p", p.id == currentId(), 0, ImVec2(0, rowH))) selId_ = p.id;
            const ImVec2 mn = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(mn.x + 10, mn.y + 6), ImGui::GetColorU32(ImGuiCol_Text), p.name.c_str());
            const std::string sub = std::to_string(p.members.size()) + (p.members.size() == 1 ? " character" : " characters");
            dl->AddText(ImVec2(mn.x + 10, mn.y + 6 + lineH() + 2), ImGui::GetColorU32(kGrey), sub.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void drawDetail() override {
        PartyStore& store = host_.parties();
        const Party* stored = store.find(currentId());
        if (!stored) {
            ImGui::Spacing();
            bigText("Party", 1.5f, kAccent);
            ImGui::TextWrapped("Create a party and add the characters that play together: one message reaches all of them.");
            if (ImGui::Button("Create a party")) {
                Party p;
                p.name = "New party";
                if (store.save(p)) selId_ = p.id;
            }
            return;
        }
        Party p = *stored;                                 // edited as a copy, saved when something changed
        bool changed = false;
        ImGui::PushID(p.id.c_str());

        changed |= inputStr("##name", p.name, U(320), "Party name");
        ImGui::SameLine();
        if (ImGui::Button("Delete party")) ImGui::OpenPopup("Delete this party?");
        if (ImGui::BeginPopupModal("Delete this party?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("\"%s\" will be removed. Its characters are not affected.", p.name.c_str());
            if (ImGui::Button("Delete")) {
                store.remove(p.id);
                selId_.clear();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();

        ImGui::BeginChild("##partybody", ImVec2(0, 0));
        changed |= drawMembers(p);
        ImGui::Spacing();
        ImGui::TextColored(kGold, "Notes");
        changed |= inputMultiline("##notes", p.notes, ImVec2(-FLT_MIN, U(100)));
        ImGui::EndChild();
        if (changed) store.save(p);
        ImGui::PopID();
    }

private:
    // The party in front: the selected one, or the first if nothing was picked yet.
    std::string currentId() const {
        const auto& all = host_.parties().all();
        if (all.empty()) return {};
        return host_.parties().find(selId_) ? selId_ : all.front().id;
    }

    bool drawMembers(Party& p) {
        CharacterStore& chars = host_.characters();
        IEncounterSink* encounter = serviceOf<IEncounterSink>(host_);
        IMessenger* messenger = serviceOf<IMessenger>(host_);
        ICharacters* sheets = serviceOf<ICharacters>(host_);
        bool changed = false;

        ImGui::TextColored(kAccent, "Characters (%d)", static_cast<int>(p.members.size()));
        if (ImGui::SmallButton("Add character…")) ImGui::OpenPopup("##addmember");
        ImGui::SameLine();
        if (ImGui::SmallButton("Add all characters")) {
            for (const Character& c : chars.all())
                if (!p.has(c.id)) p.members.push_back(c.id);
            changed = true;
        }
        if (encounter) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Add the party to the encounter"))
                for (const std::string& id : p.members)
                    if (const Character* c = chars.find(id)) encounter->addPlayer(*c);
        }
        if (messenger) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Message the party…")) messenger->compose("party:" + p.id, "", "");
        }
        if (IMasterScreen* screen = serviceOf<IMasterScreen>(host_)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Pin")) screen->pinParty(p.id);
        }
        if (ImGui::BeginPopup("##addmember")) {
            bool any = false;
            for (const Character& c : chars.all()) {
                if (p.has(c.id)) continue;
                any = true;
                ImGui::PushID(c.id.c_str());
                if (ImGui::Selectable(c.displayName().c_str())) {
                    p.members.push_back(c.id);
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                ImGui::TextColored(kGrey, "%s %s", c.kin.name.c_str(), c.profession.name.c_str());
                ImGui::PopID();
            }
            if (!any) ImGui::TextColored(kGrey, chars.all().empty() ? "There are no characters yet (Characters module)." : "Every character is already in the party.");
            ImGui::EndPopup();
        }

        if (p.members.empty()) {
            ImGui::TextColored(kGrey, "Nobody in the party yet.");
            return changed;
        }
        if (ImGui::BeginTable("##members", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthStretch, 2.2f);
            ImGui::TableSetupColumn("Kin and profession", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("HP / WP", ImGuiTableColumnFlags_WidthFixed, U(90));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, U(130));
            ImGui::TableHeadersRow();
            int remove = -1;
            for (size_t i = 0; i < p.members.size(); ++i) {
                const Character* c = chars.find(p.members[i]);
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (c) ImGui::TextUnformatted(c->displayName().c_str());
                else ImGui::TextColored(kRed, "(missing character)");
                ImGui::TableSetColumnIndex(1);
                if (c) ImGui::TextColored(kGrey, "%s %s · %s", c->kin.name.c_str(), c->profession.name.c_str(), ageRule(c->age).label);
                ImGui::TableSetColumnIndex(2);
                if (c) ImGui::TextColored(c->hp * 2 <= maxHp(*c) ? kGold : ImGui::GetStyle().Colors[ImGuiCol_Text], "%d/%d · %d/%d", c->hp, maxHp(*c), c->wp, maxWp(*c));
                ImGui::TableSetColumnIndex(3);
                if (c && sheets && ImGui::SmallButton("Sheet")) sheets->openCharacter(c->id);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (remove >= 0) {
                p.members.erase(p.members.begin() + remove);
                changed = true;
            }
        }
        return changed;
    }

    std::string selId_;
};

}  // namespace

std::unique_ptr<Module> makePartyModule(Host& host) { return std::make_unique<PartyModule>(host); }

}  // namespace gm
