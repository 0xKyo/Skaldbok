// Creatures: the Bestiary, the Rulebook's monsters, the adventure's NPCs and any homebrew creature.
#include <algorithm>
#include <cstdlib>

#include <imgui.h>

#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

class CreaturesModule : public Module {
public:
    explicit CreaturesModule(Host& host) : Module(host) { onContentChanged(); }

    const char* id() const override { return "creatures"; }
    const char* title() const override { return "Creatures"; }
    const char* summary() const override { return "Creature stat blocks, art and attack tables; add them to the encounter or send their picture to the players."; }
    const char* group() const override { return "Reference"; }
    int badge() const override { return static_cast<int>(list_.all.size()); }
    bool handles(Kind k) const override { return k == Kind::Monster; }

    bool findByName(const std::string& want, Selection& out) const override {
        for (const ListItem& it : list_.all)
            if (lowered(it.name) == want) {
                out = {Kind::Monster, it.id};
                return true;
            }
        return false;
    }

    void onContentChanged() override {
        list_.set(host_.content().listMonsters());
        loaded_ = 0;
    }

    void onSelect(const Selection& s) override {
        list_.ensureVisible(host_, s);
        load(s.id);
    }

    void drawList() override {
        if (const ListItem* it = list_.draw(host_, "Filter creatures…", host_.selection())) host_.goTo(Kind::Monster, it->id);
    }

    void drawSelection(const Selection& s) override {
        if (loaded_ != s.id) load(s.id);
        const Monster* mp = host_.content().monster(s.id);
        if (!mp) return;
        const Monster& m = *mp;
        std::string sub = m.kind == "npc" ? "NPC" : m.kind == "animal" ? "animal" : "creature";
        if (!m.category.empty()) sub += " · " + m.category;
        detailHeader(host_, m.name, sub, m.sourceId, m.ref, m.pageNote);
        if (!versions_.empty()) {
            ImGui::TextColored(kGrey, "Also in:");
            for (const ListItem& v : versions_) {
                ImGui::SameLine();
                ImGui::PushID(v.id);
                ImGui::PushStyleColor(ImGuiCol_Text, sourceColor(host_.content(), v.sourceId));
                const SourceInfo* si = host_.content().source(v.sourceId);
                if (ImGui::SmallButton(((si ? si->label : std::string("?")) + ": " + v.name).c_str())) host_.goTo(Kind::Monster, v.id);
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }

        IEncounterSink* encounter = serviceOf<IEncounterSink>(host_);
        IMessenger* messenger = serviceOf<IMessenger>(host_);

        // What the GM needs at the table comes first: art and stat blocks side by side, then the attack table.
        const float availW = ImGui::GetContentRegionAvail().x;
        const TextureCache::Tex* art = m.image.empty() ? nullptr : host_.textures().get(m.image);
        const bool sideBySide = art && availW > U(640.0f);
        if (art) {
            const float w = sideBySide ? std::clamp(availW * 0.34f, U(220.0f), U(300.0f)) : std::min(U(360.0f), availW);
            const float h = w * static_cast<float>(art->h) / static_cast<float>(art->w);
            ImGui::BeginGroup();
            ImGui::Image(reinterpret_cast<ImTextureID>(art->tex), ImVec2(w, h));
            if (m.imageRef.valid()) pageLink(host_, m.sourceId, m.imageRef, {});
            if (messenger && ImGui::SmallButton("Send this picture to the players…"))
                messenger->compose("", m.image, m.name);
            ImGui::EndGroup();
            if (sideBySide) ImGui::SameLine(0, 18);
        }

        ImGui::BeginGroup();
        if (!m.statsRef.empty()) {
            ImGui::TextColored(kGold, "Stats live elsewhere:");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", m.statsRef.c_str());
            for (const ListItem& c : refCreatures_) {
                ImGui::PushID(c.id);
                const SourceInfo* si = host_.content().source(c.sourceId);
                if (ImGui::Button(("Open " + c.name + "  (" + (si ? si->label : std::string("?")) + ")").c_str())) host_.goTo(Kind::Monster, c.id);
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
        for (size_t bi = 0; bi < m.blocks.size(); ++bi) {
            const StatBlock& b = m.blocks[bi];
            ImGui::PushID(static_cast<int>(bi));
            ImGui::Spacing();
            bigText(b.variant.empty() ? "Stat block" : ("Stat block · " + b.variant).c_str(), 1.15f, kAccent);
            if (encounter && ImGui::SmallButton("Add to encounter")) encounter->addCreature(m, &b);
            fieldsTable(b.fields, "##stat");
            ImGui::PopID();
        }
        ImGui::EndGroup();

        attacksTable(host_, m, rolledAttack_, rolledValue_);

        if (!m.abilities.empty()) {
            ImGui::Spacing();
            bigText("Abilities", 1.15f, kAccent);
            for (const NamedText& a : m.abilities) {
                ImGui::TextColored(kGold, "%s%s", a.kind == "pc_ability" ? "PC ability: " : "", a.name.c_str());
                ImGui::SameLine(0, 6);
                ImGui::TextWrapped("%s", a.text.c_str());
            }
        }
        if (!m.quote.empty() || !m.description.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            if (!m.quote.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
                paragraphs(m.quote);
                ImGui::PopStyleColor();
            }
            if (!m.description.empty()) paragraphs(m.description);
        }
        if (!m.randomEncounter.empty() && ImGui::CollapsingHeader("Random encounter", ImGuiTreeNodeFlags_DefaultOpen)) paragraphs(m.randomEncounter);
        if (!m.adventureSeed.empty() && ImGui::CollapsingHeader("Adventure seed", ImGuiTreeNodeFlags_DefaultOpen)) paragraphs(m.adventureSeed);
        if (!m.tables.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kGrey, "Related tables:");
            for (const TableRef& t : m.tables) {
                ImGui::SameLine();
                if (ImGui::SmallButton(t.title.c_str())) host_.goTo(Kind::Table, t.id);
            }
        }
    }

private:
    // Work out what depends on the creature only once per selection.
    void load(int id) {
        loaded_ = id;
        rolledAttack_ = -1;
        rolledValue_ = 0;
        versions_.clear();
        refCreatures_.clear();
        const Monster* m = host_.content().monster(id);
        if (!m) return;
        versions_ = host_.content().creatureVersions(m->name, m->id);
        // "... stats as per page 87 in the Rulebook": find the creature(s) printed on that page so the note becomes a link.
        const std::string& t = m->statsRef;
        const size_t pg = t.rfind("page ");
        if (pg == std::string::npos) return;
        const int printed = std::atoi(t.c_str() + pg + 5);
        if (printed <= 0) return;
        const int source = host_.content().sourceId("core", t.contains("Bestiary") ? "bestiary" : "rulebook");
        refCreatures_ = host_.content().creaturesOnPrintedPage(source, printed);
        std::erase_if(refCreatures_, [&](const ListItem& i) { return i.id == m->id; });
    }

    ListView list_;
    int loaded_ = 0;
    int rolledAttack_ = -1, rolledValue_ = 0;
    std::vector<ListItem> versions_, refCreatures_;
};

}  // namespace

std::unique_ptr<Module> makeCreaturesModule(Host& host) { return std::make_unique<CreaturesModule>(host); }

}  // namespace gm
