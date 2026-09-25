// Catalog pages: one kind of content per page (creatures, spells, abilities, skills, kin, professions...), as a list on
// the left and the picked entry on the right, with the data file's own intro (its chapter of the book) as an "Intro"
// tab when it has one. Every kind that model.cpp gives a page of its own gets one of these, made from that table, so
// a new kind of content needs no page code; only a kind with a detail of its own (creatures' stat blocks) adds some.
#include <algorithm>
#include <cstdlib>

#include <imgui.h>

#include "ui/homebrew_forms.h"
#include "ui/modules/modules.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

class CatalogModule : public Module {
public:
    CatalogModule(Host& host, Kind kind) : Module(host), kind_(kind), forms_(host) {}

    const char* id() const override { return kindPage(kind_); }
    const char* title() const override { return kindTitle(kind_); }
    const char* group() const override { return "Reference"; }
    Layout layout() const override { return Layout::Full; }     // the Intro tab, then its own list + detail
    int badge() const override { return static_cast<int>(list_.all.size()); }
    bool handles(Kind k, int) const override { return k == kind_; }

    bool findByName(const std::string& want, Selection& out) const override {
        for (const ListItem& it : list_.all)
            if (lowered(it.name) == want) {
                out = {it.kind, it.id};
                return true;
            }
        return false;
    }

    void onContentChanged() override {
        list_.set(items());
        current_ = {};
    }

    void onSelect(const Selection& s) override {        // a jump to one entry (the list, search, a link, history)
        current_ = s;
        list_.ensureVisible(host_, s);
        tabs_.wantPage = true;
        picked(s);
    }

    void drawFull() override {
        introTabs(tabs_, "##tabs", kindTitle(kind_), host_.content().introOf(kind_), [&] { drawListDetail(); });
        forms_.draw();
    }

    void drawList() override {
        if (HomebrewForms::supports(kind_)) {
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            if (ImGui::Button(("+ Generate " + std::string(kindLabel(kind_))).c_str())) forms_.open(kind_, nullptr);
            ImGui::PopItemFlag();
        }
        const std::string hint = "Filter " + lowered(kindTitle(kind_)) + "…";
        if (const ListItem* it = list_.draw(host_, hint.c_str(), current_.id ? &current_ : nullptr)) host_.goTo(it->kind, it->id);
    }

    // A generic entry (Entry): its header, fields, text, own tables and page. Kinds with more than that override it.
    void drawSelection(const Selection& s) override {
        const Entry* e = host_.content().entry(s.kind, s.id);
        if (!e) return;
        detailHeader(host_, e->title, e->subtitle, e->sourceId, s);
        if (!e->editedBy.empty()) ImGui::TextColored(kGold, "Changed by %s", e->editedBy.c_str());
        if (HomebrewForms::supports(kind_)) {
            if (ImGui::SmallButton("Edit")) forms_.open(kind_, e);
            if (const std::string del = HomebrewForms::deleteLabel(*e); !del.empty()) {     // only what the GM's own pack holds
                ImGui::SameLine();
                if (ImGui::SmallButton(del.c_str())) forms_.askDelete(*e);
            }
        }
        if (!e->image.empty())
            if (const TextureCache::Tex* t = host_.textures().get(e->image)) {
                const float w = std::min(U(220), ImGui::GetContentRegionAvail().x);
                ImGui::Image(reinterpret_cast<ImTextureID>(t->tex), ImVec2(w, w * static_cast<float>(t->h) / static_cast<float>(t->w)));
            }
        if (!e->fields.empty()) fieldsTable(e->fields, "##fields");
        ImGui::Spacing();
        paragraphs(e->body, "body");
        for (size_t i = 0; i < e->tables.size(); ++i) {
            const DataTable& t = e->tables[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Spacing();
            bigText((t.title + (t.dice.empty() ? "" : " · " + t.dice)).c_str(), 1.1f, kAccent);
            tableGrid(t, -1);
            ImGui::PopID();
        }
        ImGui::Spacing();
        pageLink(host_, e->sourceId, e->ref, e->pageNote);
    }

protected:
    // The rows of the list: every entry of the kind, by name.
    virtual std::vector<ListItem> items() {
        std::vector<ListItem> out;
        for (const Entry& e : host_.content().entries(kind_)) {
            const std::string sub = e.subtitle == kindLabel(kind_) ? "" : e.subtitle;   // "Kin" on every kin says nothing
            out.push_back({e.id, e.kind, e.title, sub, e.sourceId, e.ref.page});
        }
        std::ranges::sort(out, [](const ListItem& a, const ListItem& b) { return lowered(a.name) < lowered(b.name); });
        return out;
    }
    virtual void picked(const Selection&) {}            // an entry was just picked: work out what its detail needs, once

    Kind kind_;

private:
    void drawListDetail() {
        const float listW = std::clamp(ImGui::GetContentRegionAvail().x * 0.3f, U(280.0f), U(420.0f));
        ImGui::BeginChild("##list", ImVec2(listW, 0), ImGuiChildFlags_Borders);
        drawList();
        ImGui::EndChild();
        ImGui::SameLine();
        // nothing picked yet (or the pick was filtered away): the first row, so the page never opens empty
        if (!current_.id && !list_.shown.empty()) {
            const ListItem& first = list_.all[static_cast<size_t>(list_.shown.front())];
            current_ = {first.kind, first.id};
            picked(current_);
        }
        ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (current_.id) drawSelection(current_);
        else ImGui::TextColored(kGrey, "Nothing here yet.");
        ImGui::EndChild();
    }

    ListView list_;
    Selection current_{};                                // this page's own pick: each page remembers its own
    IntroTabs tabs_;
    HomebrewForms forms_;
};

// Creatures have a detail of their own: art, stat blocks, the attack table, abilities, and their links.
class CreaturesModule : public CatalogModule {
public:
    explicit CreaturesModule(Host& host) : CatalogModule(host, Kind::Monster) {}

    void drawSelection(const Selection& s) override {
        if (loaded_ != s.id) picked(s);
        const Monster* mp = host_.content().monster(s.id);
        if (!mp) return;
        const Monster& m = *mp;
        std::string sub = m.kind == "npc" ? "NPC" : m.kind == "animal" ? "animal" : "creature";
        if (!m.category.empty()) sub += " · " + m.category;
        detailHeader(host_, m.name, sub, m.sourceId, s);
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
            if (messenger && ImGui::SmallButton("Send this picture to the players…")) messenger->compose("", m.image, m.name);
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
            ImGui::PushID("abilities");
            for (size_t i = 0; i < m.abilities.size(); ++i) {
                const NamedText& a = m.abilities[i];
                ImGui::TextColored(kGold, "%s%s", a.kind == "pc_ability" ? "PC ability: " : "", a.name.c_str());
                ImGui::SameLine(0, 6);
                copyableText(("##a" + std::to_string(i)).c_str(), a.text);
            }
            ImGui::PopID();
        }
        if (!m.quote.empty() || !m.description.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            if (!m.quote.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
                paragraphs(m.quote, "quote");
                ImGui::PopStyleColor();
            }
            if (!m.description.empty()) paragraphs(m.description, "desc");
        }
        if (!m.randomEncounter.empty() && ImGui::CollapsingHeader("Random encounter", ImGuiTreeNodeFlags_DefaultOpen)) paragraphs(m.randomEncounter, "encounter");
        if (!m.adventureSeed.empty() && ImGui::CollapsingHeader("Adventure seed", ImGuiTreeNodeFlags_DefaultOpen)) paragraphs(m.adventureSeed, "seed");
        if (!m.tables.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kGrey, "Related tables:");
            for (const TableRef& t : m.tables) {
                ImGui::SameLine();
                if (ImGui::SmallButton(t.title.c_str())) host_.goTo(Kind::Table, t.id);
            }
        }
        ImGui::Spacing();
        pageLink(host_, m.sourceId, m.ref, m.pageNote);
    }

protected:
    std::vector<ListItem> items() override { return host_.content().listMonsters(); }

    // Work out what depends on the creature only once per pick.
    void picked(const Selection& s) override {
        loaded_ = s.id;
        rolledAttack_ = -1;
        rolledValue_ = 0;
        versions_.clear();
        refCreatures_.clear();
        const Monster* m = host_.content().monster(s.id);
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

private:
    int loaded_ = 0;
    int rolledAttack_ = -1, rolledValue_ = 0;
    std::vector<ListItem> versions_, refCreatures_;
};

}  // namespace

std::unique_ptr<Module> makeCatalogModule(Host& host, Kind kind) {
    std::unique_ptr<Module> m;
    if (kind == Kind::Monster) m = std::make_unique<CreaturesModule>(host);
    else m = std::make_unique<CatalogModule>(host, kind);
    m->onContentChanged();              // not from the constructor: items() is virtual
    return m;
}

}  // namespace gm
