// Mosaics: spells, abilities, skills, kin, professions and equipment as grids of reference cards.
//
// Every card in a mosaic has the same size (the size of a reference card: "Assassin" for abilities, the 70th percentile of
// content height for the others) so the grid stays tidy. A card whose text does not fit is cut off with a fade and can be
// clicked: it then keeps its width and grows taller, showing everything; the cards below it in the same column move down
// and nothing else changes place. One module class serves all of them: what differs is only which kinds of entry it lists.
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

struct Def {
    const char* id;
    const char* title;
    const char* summary;
    std::vector<Kind> kinds;
    const char* hint;
    const char* refName;                            // every card takes the size of this one; null = 70th percentile
    std::vector<std::string> tagOrder;              // the filter chips that come first, in this order
};

const Def& defOf(Compendium c) {
    static const Def defs[] = {
        {"spells", "Spells", "Spells and magic tricks of the books and of homebrew packs, filtered by school.", {Kind::Spell},
         "Filter spells (name, school, rank, text)…", nullptr, {"General Magic", "Animism", "Elementalism", "Mentalism", "Spells", "Tricks"}},
        {"abilities", "Abilities", "Heroic and innate abilities.", {Kind::Ability}, "Filter abilities (name, requirement, text)…", "Assassin",
         {"Heroic", "Innate"}},
        {"skills", "Skills", "Core, weapon and magic skills.", {Kind::Skill}, "Filter skills (name, attribute, text)…", nullptr, {"Core", "Weapon", "Magic"}},
        {"kin", "Kin", "The playable kin and their innate abilities.", {Kind::Kin}, "Filter kin…", nullptr, {}},
        {"professions", "Professions", "The professions: key attribute, skills, heroic ability.", {Kind::Profession}, "Filter professions…", nullptr, {}},
        {"equipment", "Equipment", "Weapons, armor and gear.", {Kind::Weapon, Kind::Armor, Kind::Gear}, "Filter equipment (name, damage, cost, text)…", nullptr,
         {"Weapons", "Armor", "Gear"}},
    };
    return defs[static_cast<int>(c)];
}

struct Metrics {
    float pad, gap, footer, titleSize;
};

Metrics metrics() {
    Metrics m;
    m.pad = U(12);
    m.gap = U(12);
    m.footer = ImGui::GetFontSize() + U(6) + U(12);      // link button + bottom padding
    m.titleSize = ImGui::GetFontSize() * 1.25f;
    return m;
}

// Calls fn(line) for every '\n'-separated line of text.
template <class F>
void eachLine(const std::string& text, F fn) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        fn(text.substr(pos, end - pos));
        pos = end + 1;
    }
}

// Height of the card's content (top padding included, footer excluded) when laid out `w` pixels wide.
// The drawing code below advances by exactly the same amounts.
float contentHeight(const Entry& e, float w, const Metrics& m) {
    const float inner = w - 2 * m.pad;
    const float lh = ImGui::GetTextLineHeight();
    float h = m.pad + m.titleSize + U(2) + lh + U(6);
    for (const Field& f : e.fields) {
        const float lw = ImGui::CalcTextSize((f.label + ":").c_str()).x + U(6);
        h += std::max(lh, ImGui::CalcTextSize(f.value.c_str(), nullptr, false, inner - lw).y) + U(3);
    }
    h += U(6);
    eachLine(e.body, [&](const std::string& line) {
        h += line.empty() ? U(5) : ImGui::CalcTextSize(line.c_str(), nullptr, false, inner).y + U(4);
    });
    return h;
}

class CompendiumModule : public Module {
public:
    CompendiumModule(Host& host, Compendium which) : Module(host), def_(defOf(which)), which_(which) { onContentChanged(); }

    const char* id() const override { return def_.id; }
    const char* title() const override { return def_.title; }
    const char* summary() const override { return def_.summary; }
    const char* group() const override { return "Reference"; }
    Layout layout() const override { return Layout::Full; }
    int badge() const override { return static_cast<int>(cards_.size()); }
    bool handles(Kind k) const override { return std::ranges::contains(def_.kinds, k); }

    bool findByName(const std::string& want, Selection& out) const override {
        for (const Card& c : cards_)
            if (lowered(c.e->title) == want) {
                out = {c.e->kind, c.e->id};
                return true;
            }
        return false;
    }

    void onContentChanged() override {
        cards_.clear();
        heightsKey_ = {-1.0f, -1.0f};
        for (Kind k : def_.kinds)
            for (const Entry& e : host_.content().entries(k)) cards_.push_back({&e, tagsFor(e), searchText(e)});
        if (which_ == Compendium::Abilities)
            std::ranges::stable_sort(cards_, [](const Card& a, const Card& b) {
                if (a.tags[0] != b.tags[0]) return a.tags[0] == "Heroic";
                return lowered(a.e->title) < lowered(b.e->title);
            });
        // filter chips: the usual ones first, then whatever else the packs brought (a homebrew school of magic...)
        tagOptions_.clear();
        std::set<std::string> present;
        for (const Card& c : cards_) present.insert(c.tags.begin(), c.tags.end());
        for (const std::string& t : def_.tagOrder)
            if (present.count(t)) tagOptions_.push_back(t);
        for (const std::string& t : present)
            if (!std::ranges::contains(tagOptions_, t)) {
                // an unknown school sits with the schools, before "Spells"/"Tricks"
                auto pos = which_ == Compendium::Spells ? std::ranges::find(tagOptions_, "Spells") : tagOptions_.end();
                tagOptions_.insert(pos, t);
            }
        tagsOff_.clear();
        expanded_.clear();
    }

    void onSelect(const Selection& s) override {          // jump to the card and make sure filters do not hide it
        const Entry* e = host_.content().entry(s.kind, s.id);
        if (!e) return;
        focusKey_ = e->key;
        scroll_ = true;
        filter_[0] = 0;
        tagsOff_.clear();
    }

    void drawFull() override { draw(); }

private:
    struct Card {
        const Entry* e;
        std::vector<std::string> tags;                    // a card shows only while every one of its tags is switched on
        std::string hay;                                  // everything the filter looks at, lower case, made once
        float natural = 0;                                // the height it needs to show everything, for the width and font in heightsKey_
    };

    static std::string searchText(const Entry& e) {
        std::string hay = lowered(e.title + " " + e.subtitle + " " + e.body);
        for (const Field& f : e.fields) hay += " " + lowered(f.label + " " + f.value);
        return hay;
    }

    std::vector<std::string> tagsFor(const Entry& e) const {
        switch (which_) {
            case Compendium::Spells: return {e.prop("school"), e.prop("trick") == "1" ? "Tricks" : "Spells"};
            case Compendium::Abilities: return {e.prop("type") == "heroic" ? "Heroic" : "Innate"};
            case Compendium::Skills: return {capitalized(e.prop("category").empty() ? "core" : e.prop("category"))};
            case Compendium::Equipment: return {e.kind == Kind::Weapon ? "Weapons" : e.kind == Kind::Armor ? "Armor" : "Gear"};
            default: return {};
        }
    }

    void draw() {
        const Metrics mt = metrics();

        // ---- filters ---------------------------------------------------------------------------------
        ImGui::SetNextItemWidth(U(280));
        ImGui::InputTextWithHint("##mfilter", def_.hint, filter_, sizeof filter_);
        for (size_t i = 0; i < tagOptions_.size(); ++i) {
            const std::string& tag = tagOptions_[i];
            ImGui::SameLine(0, 14);
            bool on = !tagsOff_.count(tag);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, tagColor(i));
            if (ImGui::Checkbox(tag.c_str(), &on)) {
                if (on) tagsOff_.erase(tag);
                else tagsOff_.insert(tag);
            }
            ImGui::PopStyleColor();
        }

        const std::string needle = lowered(filter_);
        std::vector<const Card*> shown;
        for (const Card& c : cards_) {
            if (!host_.sourceShown(c.e->sourceId)) continue;
            bool tagsOk = true;
            for (const std::string& t : c.tags) tagsOk &= !tagsOff_.count(t);
            if (!tagsOk) continue;
            if (!needle.empty()) {
                if (!c.hay.contains(needle)) continue;
            }
            shown.push_back(&c);
        }
        ImGui::SameLine(0, 14);
        ImGui::TextColored(kGrey, "%d of %d", static_cast<int>(shown.size()), static_cast<int>(cards_.size()));
        if (!expanded_.empty()) {
            ImGui::SameLine(0, 14);
            if (ImGui::SmallButton("Collapse all")) expanded_.clear();
        }

        // ---- geometry ----------------------------------------------------------------------------------
        ImGui::BeginChild("##cards", ImVec2(0, 0), ImGuiChildFlags_None);
        const float availW = ImGui::GetContentRegionAvail().x;
        const int cols = std::max(1, static_cast<int>((availW + mt.gap) / (U(340) + mt.gap)));
        const float cardW = (availW - mt.gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);

        // Measuring every card's text is the costly part: it is redone only when the width or the font size changes.
        const std::pair<float, float> key{cardW, ImGui::GetFontSize()};
        if (key != heightsKey_) {
            heightsKey_ = key;
            for (Card& c : cards_) c.natural = contentHeight(*c.e, cardW, mt) + mt.footer;
            // the size every card copies: the reference card, or the 70th percentile of all natural heights
            cardH_ = 0.0f;
            if (def_.refName)
                for (const Card& c : cards_)
                    if (c.e->title == def_.refName) cardH_ = c.natural;
            if (cardH_ <= 0.0f && !cards_.empty()) {
                std::vector<float> nat;
                for (const Card& c : cards_) nat.push_back(c.natural);
                std::ranges::sort(nat);
                cardH_ = nat[static_cast<size_t>(0.7f * static_cast<float>(nat.size() - 1))];
            }
            cardH_ = std::max(cardH_, U(200));
        }
        const float cardH = cardH_;

        if (shown.empty()) ImGui::TextColored(kGrey, "Nothing matches the filters.");

        struct Placed {
            const Card* card;
            float x, y, w, h;
            bool expanded;
            bool overflow;
        };
        std::vector<Placed> placed;
        const ImVec2 origin = ImGui::GetCursorPos();
        // Every card keeps its place in the grid (card i sits in column i % cols). An opened card keeps its width and
        // grows downward; only the cards beneath it, in the same column, are pushed down. Nothing is rearranged.
        std::vector<float> colY(static_cast<size_t>(cols), origin.y);
        size_t index = 0;
        for (const Card* c : shown) {
            const bool overflow = c->natural > cardH + 0.5f;
            // a jump from search or history opens a card that would otherwise be cut off
            if (overflow && scroll_ && c->e->key == focusKey_) expanded_.insert(c->e->key);
            const bool open = overflow && expanded_.count(c->e->key);
            const float h = open ? c->natural : cardH;
            const size_t col = index++ % static_cast<size_t>(cols);
            placed.push_back({c, origin.x + static_cast<float>(col) * (cardW + mt.gap), colY[col], cardW, h, open, overflow});
            colY[col] += h + mt.gap;
        }
        const float y = *std::max_element(colY.begin(), colY.end());

        // ---- drawing -----------------------------------------------------------------------------------
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 screenOrigin = ImGui::GetCursorScreenPos();
        const float lh = ImGui::GetTextLineHeight();
        IMasterScreen* screen = serviceOf<IMasterScreen>(host_);
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 bgCol = ImGui::GetColorU32(ImGuiCol_ChildBg);
        const ImU32 bgClear = bgCol & 0x00FFFFFF;

        for (const Placed& p : placed) {
            const Entry& e = *p.card->e;
            const ImVec2 tl(screenOrigin.x + (p.x - origin.x), screenOrigin.y + (p.y - origin.y));
            const ImVec2 br(tl.x + p.w, tl.y + p.h);
            const bool focused = e.key == focusKey_;
            if (focused && scroll_) {
                ImGui::SetScrollY(std::max(0.0f, p.y - U(16)));
                scroll_ = false;
            }
            size_t tagIdx = 0;
            if (!p.card->tags.empty()) {
                auto it = std::ranges::find(tagOptions_, p.card->tags.front());
                if (it != tagOptions_.end()) tagIdx = static_cast<size_t>(it - tagOptions_.begin());
            }
            const ImVec4 accent = tagColor(tagIdx);
            const bool clickable = p.overflow;

            ImGui::PushID(e.key.c_str());
            // whole-card click target; the "Original" button drawn later sits on top of it
            ImGui::SetCursorScreenPos(tl);
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##card", ImVec2(p.w, p.h));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked() && clickable) {
                if (p.expanded) expanded_.erase(e.key);
                else expanded_.insert(e.key);
            }
            if (hovered && clickable) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            dl->AddRectFilled(tl, br, bgCol, U(8));
            dl->AddRectFilled(tl, ImVec2(tl.x + U(4), br.y), ImGui::GetColorU32(accent), U(2));
            dl->AddRect(tl, br,
                        ImGui::GetColorU32(focused ? accent : (hovered && clickable ? ImVec4(0.55f, 0.48f, 0.34f, 1.0f) : ImVec4(0.725f, 0.659f, 0.518f, 1.0f))),
                        U(8), focused ? 2.0f : 1.0f);

            // content, clipped above the footer
            dl->PushClipRect(tl, ImVec2(br.x, br.y - mt.footer), true);
            const float inner = p.w - 2 * mt.pad;
            float cy = tl.y + mt.pad;
            dl->AddText(font, mt.titleSize, ImVec2(tl.x + mt.pad, cy), ImGui::GetColorU32(accent), e.title.c_str());
            cy += mt.titleSize + U(2);
            dl->AddText(font, fontSize, ImVec2(tl.x + mt.pad, cy), ImGui::GetColorU32(kGrey), e.subtitle.c_str());
            cy += lh + U(6);
            for (const Field& f : e.fields) {
                const std::string label = f.label + ":";
                const float lw = ImGui::CalcTextSize(label.c_str()).x + U(6);
                dl->AddText(font, fontSize, ImVec2(tl.x + mt.pad, cy), ImGui::GetColorU32(kGold), label.c_str());
                dl->AddText(font, fontSize, ImVec2(tl.x + mt.pad + lw, cy), textCol, f.value.c_str(), nullptr, inner - lw);
                cy += std::max(lh, ImGui::CalcTextSize(f.value.c_str(), nullptr, false, inner - lw).y) + U(3);
            }
            cy += U(6);
            eachLine(e.body, [&](const std::string& line) {
                if (line.empty()) {
                    cy += U(5);
                    return;
                }
                dl->AddText(font, fontSize, ImVec2(tl.x + mt.pad, cy), textCol, line.c_str(), nullptr, inner);
                cy += ImGui::CalcTextSize(line.c_str(), nullptr, false, inner).y + U(4);
            });
            dl->PopClipRect();

            if (p.overflow && !p.expanded) {                  // fade the cut-off text out
                const float fadeH = U(34);
                dl->AddRectFilledMultiColor(ImVec2(tl.x + U(2), br.y - mt.footer - fadeH), ImVec2(br.x - U(2), br.y - mt.footer), bgClear, bgClear,
                                            bgCol, bgCol);
            }

            // footer: link to the original page (or the homebrew source), and the expand / collapse hint
            ImGui::SetCursorScreenPos(ImVec2(tl.x + mt.pad, br.y - mt.footer + U(2)));
            pageLink(host_, e.sourceId, e.ref, e.pageNote);
            if (screen) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Pin")) screen->pinEntry(e.kind, e.id);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pin this to the Master Screen");
            }
            if (p.overflow) {
                const char* hint = p.expanded ? "Click to collapse" : "Click to expand";
                const ImVec2 hs = ImGui::CalcTextSize(hint);
                dl->AddText(font, fontSize, ImVec2(br.x - mt.pad - hs.x, br.y - mt.footer + U(2)), ImGui::GetColorU32(hovered ? accent : kGrey), hint);
            }
            ImGui::PopID();
        }

        // give the scroll area its full height
        ImGui::SetCursorPos(ImVec2(origin.x, y));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
    }

    const Def& def_;
    Compendium which_;
    std::vector<Card> cards_;
    std::pair<float, float> heightsKey_{-1.0f, -1.0f};              // (card width, font size) the cached heights were measured for
    float cardH_ = 0;                                         // the height every card takes unless opened
    std::vector<std::string> tagOptions_;
    std::set<std::string> tagsOff_;
    char filter_[64] = {};
    std::set<std::string> expanded_;                      // cards opened to full size (by entry key)
    std::string focusKey_;                                // card highlighted after a jump from search / history
    bool scroll_ = false;
};

}  // namespace

std::unique_ptr<Module> makeCompendiumModule(Host& host, Compendium which) { return std::make_unique<CompendiumModule>(host, which); }

}  // namespace gm
