// Mosaics: spells, abilities, skills, kin and professions as grids of reference cards.
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

#include <SDL3/SDL.h>

#include "filedialog.h"
#include "jsonutil.h"
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

// A table drawn inside an opened card: a title line, a line with the column names, and a row per roll. The height and the drawing
// use these same numbers.
struct TableGeom {
    float rollW = 0, colW = 0, titleH = 0, headerH = 0, total = 0;
    std::vector<float> rowH;
};

TableGeom tableGeom(const DataTable& t, float w) {
    TableGeom g;
    const float lh = ImGui::GetTextLineHeight();
    g.titleH = lh + U(4);
    g.headerH = lh + U(4);
    g.rollW = t.dice.empty() ? 0.0f : U(48);
    const size_t cols = std::max<size_t>(1, t.columns.size());
    g.colW = (w - g.rollW) / static_cast<float>(cols);
    g.total = g.titleH + g.headerH;
    for (const TableRow& r : t.rows) {
        float h = lh;
        for (size_t c = 0; c < r.cells.size() && c < cols; ++c) h = std::max(h, ImGui::CalcTextSize(r.cells[c].c_str(), nullptr, false, g.colW - U(8)).y);
        g.rowH.push_back(h + U(4));
        g.total += g.rowH.back();
    }
    g.total += U(8);
    return g;
}

// The extra height an opened card needs for its own tables (0 when it has none).
float tablesHeight(const Entry& e, float w, const Metrics& m) {
    if (e.tables.empty()) return 0.0f;
    float h = U(6);
    for (const DataTable& t : e.tables) h += tableGeom(t, w - 2 * m.pad).total;
    return h;
}

// A card's own tables (a kin's first names...), shown when it is opened. The title and headers are structural labels (not
// copyable, like a table's headers anywhere else); every cell is real, selectable text.
void drawCardTables(const Entry& e, float x, float y, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImU32 gold = ImGui::GetColorU32(kGold), grey = ImGui::GetColorU32(kGrey), line = ImGui::GetColorU32(ImVec4(0.725f, 0.659f, 0.518f, 0.6f));
    ImGui::PushID("tables");
    for (size_t ti = 0; ti < e.tables.size(); ++ti) {
        const DataTable& t = e.tables[ti];
        ImGui::PushID(static_cast<int>(ti));
        const TableGeom g = tableGeom(t, w);
        dl->AddText(font, fontSize, ImVec2(x, y), gold, (t.title + (t.dice.empty() ? "" : " · " + t.dice)).c_str());
        y += g.titleH;
        if (g.rollW > 0) dl->AddText(font, fontSize, ImVec2(x, y), grey, t.dice.c_str());
        for (size_t c = 0; c < t.columns.size(); ++c) dl->AddText(font, fontSize, ImVec2(x + g.rollW + static_cast<float>(c) * g.colW, y), grey, t.columns[c].c_str());
        y += g.headerH;
        dl->AddLine(ImVec2(x, y - U(2)), ImVec2(x + w, y - U(2)), line);
        for (size_t r = 0; r < t.rows.size(); ++r) {
            const TableRow& row = t.rows[r];
            if (g.rollW > 0) dl->AddText(font, fontSize, ImVec2(x, y), gold, row.rollText.c_str());
            for (size_t c = 0; c < row.cells.size() && c < std::max<size_t>(1, t.columns.size()); ++c) {
                ImGui::SetCursorScreenPos(ImVec2(x + g.rollW + static_cast<float>(c) * g.colW, y));
                copyableText(("##r" + std::to_string(r) + "_" + std::to_string(c)).c_str(), row.cells[c], g.colW - U(8));
            }
            y += g.rowH[r];
            dl->AddLine(ImVec2(x, y - U(2)), ImVec2(x + w, y - U(2)), line);
        }
        y += U(8);
        ImGui::PopID();
    }
    ImGui::PopID();
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
    bool handles(Kind k, int) const override { return std::ranges::contains(def_.kinds, k); }

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
        jumpToCards_ = true;   // a specific card was reached (search, a link...): show it, not the Intro tab
    }

    void drawFull() override { draw(); }

private:
    struct Card {
        const Entry* e;
        std::vector<std::string> tags;                    // a card shows only while every one of its tags is switched on
        std::string hay;                                  // everything the filter looks at, lower case, made once
        float natural = 0;                                // the height its text needs, footer included, for the width and font in heightsKey_
        float tablesH = 0;                                // what its own tables add when it is opened (they are not part of the collapsed size)
    };

    static std::string searchText(const Entry& e) {
        std::string hay = lowered(e.title + " " + e.subtitle + " " + e.body);
        for (const Field& f : e.fields) hay += " " + lowered(f.label + " " + f.value);
        for (const DataTable& t : e.tables) {
            hay += " " + lowered(t.title);
            for (const TableRow& r : t.rows)
                for (const std::string& c : r.cells) hay += " " + lowered(c);
        }
        return hay;
    }

    std::vector<std::string> tagsFor(const Entry& e) const {
        switch (which_) {
            case Compendium::Spells: return {e.prop("school"), e.prop("trick") == "1" ? "Tricks" : "Spells"};
            case Compendium::Abilities: return {e.prop("type") == "heroic" ? "Heroic" : "Innate"};
            case Compendium::Skills: return {capitalized(e.prop("category").empty() ? "core" : e.prop("category"))};
            default: return {};
        }
    }

    // A category with its own "intro" (Skills, Abilities) is split into two tabs: the explanation, and the cards
    // themselves - "todo junto" but not sharing one long scroll. A category with no intro just shows its cards, as
    // before, with no tab bar to click through for nothing.
    void draw() {
        const Metrics mt = metrics();
        const Intro& intro = host_.content().introOf(def_.kinds.empty() ? Kind::Spell : def_.kinds.front());
        if (intro.empty()) {
            drawMosaic(mt);
            return;
        }
        if (!ImGui::BeginTabBar("##compendiumtabs")) return;
        if (ImGui::BeginTabItem("Intro")) {
            drawIntro(intro);
            ImGui::EndTabItem();
        }
        const ImGuiTabItemFlags flags = jumpToCards_ ? ImGuiTabItemFlags_SetSelected : 0;
        jumpToCards_ = false;
        if (ImGui::BeginTabItem(def_.title, nullptr, flags)) {
            drawMosaic(mt);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void drawIntro(const Intro& intro) {
        ImGui::BeginChild("##intro", ImVec2(0, 0));
        if (!intro.body.empty()) paragraphs(intro.body, "introbody");
        for (size_t i = 0; i < intro.sections.size(); ++i) {
            const RuleNode::Section& sec = intro.sections[i];
            ImGui::Spacing();
            bigText(sec.title.c_str(), 1.1f, kGold);
            paragraphs(sec.body, ("introsec" + std::to_string(i)).c_str());
        }
        ImGui::EndChild();
    }

    void drawMosaic(const Metrics& mt) {
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
        if (which_ == Compendium::Kin) {
            ImGui::SameLine(0, 14);
            if (ImGui::Button("+ Generate Kin")) openKinForm(nullptr);
        }
        if (which_ == Compendium::Abilities) {
            ImGui::SameLine(0, 14);
            if (ImGui::Button("+ Generate Ability")) openAbilityForm(nullptr);
        }
        drawKinForm();
        drawAbilityForm();

        // ---- geometry ----------------------------------------------------------------------------------
        ImGui::BeginChild("##cards", ImVec2(0, 0), ImGuiChildFlags_None);
        const float availW = ImGui::GetContentRegionAvail().x;
        const int cols = std::max(1, static_cast<int>((availW + mt.gap) / (U(340) + mt.gap)));
        const float cardW = (availW - mt.gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);

        // Measuring every card's text is the costly part: it is redone only when the width or the font size changes.
        const std::pair<float, float> key{cardW, ImGui::GetFontSize()};
        if (key != heightsKey_) {
            heightsKey_ = key;
            for (Card& c : cards_) {
                c.natural = contentHeight(*c.e, cardW, mt) + mt.footer;
                c.tablesH = tablesHeight(*c.e, cardW, mt);
            }
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
            bool overflow;                 // there is more to see than the collapsed card shows: cut-off text, or its own tables
            bool textCut;                  // the text itself is cut off (the fade)
        };
        std::vector<Placed> placed;
        const ImVec2 origin = ImGui::GetCursorPos();
        // Every card keeps its place in the grid (card i sits in column i % cols). An opened card keeps its width and
        // grows downward; only the cards beneath it, in the same column, are pushed down. Nothing is rearranged.
        std::vector<float> colY(static_cast<size_t>(cols), origin.y);
        size_t index = 0;
        for (const Card* c : shown) {
            const bool textCut = c->natural > cardH + 0.5f;
            const bool overflow = textCut || c->tablesH > 0;
            // a jump from search or history opens a card that would otherwise be cut off
            if (overflow && scroll_ && c->e->key == focusKey_) expanded_.insert(c->e->key);
            const bool open = overflow && expanded_.count(c->e->key);
            const float h = open ? std::max(cardH, c->natural + c->tablesH) : cardH;
            const size_t col = index++ % static_cast<size_t>(cols);
            placed.push_back({c, origin.x + static_cast<float>(col) * (cardW + mt.gap), colY[col], cardW, h, open, overflow, textCut});
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

            ImGui::PushID(e.key.c_str());

            dl->AddRectFilled(tl, br, bgCol, U(8));
            dl->AddRectFilled(tl, ImVec2(tl.x + U(4), br.y), ImGui::GetColorU32(accent), U(2));
            dl->AddRect(tl, br, ImGui::GetColorU32(focused ? accent : ImVec4(0.725f, 0.659f, 0.518f, 1.0f)), U(8), focused ? 2.0f : 1.0f);

            if (screen) {
                const float btnW = ImGui::CalcTextSize("Pin").x + ImGui::GetStyle().FramePadding.x * 2;
                ImGui::SetCursorScreenPos(ImVec2(br.x - mt.pad - btnW, tl.y + mt.pad));
                if (ImGui::SmallButton("Pin")) screen->pinEntry(e.kind, e.id);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pin this to the Master Screen");
            }

            // content, clipped above the footer: real widgets (not drawn straight onto the draw list) so every word of it can be
            // selected and copied, like any other text in the app. Only the card itself (its background/border) and the explicit
            // "Click to expand" hint below act as buttons; everything else here is plain, selectable content.
            // A copyableText() that is tall (a long, unwrapped line) opens its own child window internally (InputTextMultiline in
            // multiline mode always does): a draw-list-only clip (dl->PushClipRect) does not reach into a child window's own draw
            // list, so it would render past the card's bottom edge, into the row below. ImGui::PushClipRect updates the *window's*
            // clip state instead, which a child window created afterward inherits (intersected with its own bounds) - this is what
            // actually keeps it inside the card.
            const ImVec2 clipMin(tl.x, tl.y), clipMax(br.x, br.y - mt.footer);
            ImGui::PushClipRect(clipMin, clipMax, true);
            const float inner = p.w - 2 * mt.pad;
            float cy = tl.y + mt.pad;

            ImGui::PushFont(nullptr, mt.titleSize);
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::SetCursorScreenPos(ImVec2(tl.x + mt.pad, cy));
            copyableText("##title", e.title, inner);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            cy += mt.titleSize + U(2);

            ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
            ImGui::SetCursorScreenPos(ImVec2(tl.x + mt.pad, cy));
            copyableText("##subtitle", e.subtitle, inner);
            ImGui::PopStyleColor();
            cy += lh + U(6);

            for (size_t i = 0; i < e.fields.size(); ++i) {
                if (cy > clipMax.y) break;                    // same reasoning as the body loop below
                const Field& f = e.fields[i];
                const std::string label = f.label + ":";
                const float lw = ImGui::CalcTextSize(label.c_str()).x + U(6);
                ImGui::SetCursorScreenPos(ImVec2(tl.x + mt.pad, cy));
                ImGui::TextColored(kGold, "%s", label.c_str());
                ImGui::SameLine(0, U(6));
                copyableText(("##f" + std::to_string(i)).c_str(), f.value, inner - lw);
                cy += std::max(lh, ImGui::CalcTextSize(f.value.c_str(), nullptr, false, inner - lw).y) + U(3);
            }
            cy += U(6);
            size_t lineIdx = 0;
            eachLine(e.body, [&](const std::string& line) {
                if (line.empty()) {
                    cy += U(5);
                    return;
                }
                // A collapsed, cut-off card still has its whole text here (only the fade hides the rest): once a line starts
                // below the visible area, stop placing widgets for the lines after it - dangling ones the fade merely hides
                // would still sit there, interactive, past the bottom of the card (and, worse, below the cards underneath it).
                if (cy > clipMax.y) return;
                ImGui::SetCursorScreenPos(ImVec2(tl.x + mt.pad, cy));
                copyableText(("##b" + std::to_string(lineIdx++)).c_str(), line, inner);
                cy += ImGui::CalcTextSize(line.c_str(), nullptr, false, inner).y + U(4);
            });
            if (p.expanded && !e.tables.empty()) drawCardTables(e, tl.x + mt.pad, cy + U(6), inner);
            ImGui::PopClipRect();

            if (p.textCut && !p.expanded) {                   // fade the cut-off text out
                const float fadeH = U(34);
                dl->AddRectFilledMultiColor(ImVec2(tl.x + U(2), br.y - mt.footer - fadeH), ImVec2(br.x - U(2), br.y - mt.footer), bgClear, bgClear,
                                            bgCol, bgCol);
            }

            // footer: the book reference (a footnote, not a link any more), an Edit button (Kin and Abilities, for now)
            // and the expand / collapse hint
            ImGui::SetCursorScreenPos(ImVec2(tl.x + mt.pad, br.y - mt.footer + U(2)));
            pageLink(host_, e.sourceId, e.ref, e.pageNote);
            if (which_ == Compendium::Kin) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Edit")) openKinForm(&e);
            }
            if (which_ == Compendium::Abilities) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Edit")) openAbilityForm(&e);
            }
            if (p.overflow) {
                const char* hint = p.expanded ? "Click to collapse" : "Click to expand";
                const ImVec2 hs = ImGui::CalcTextSize(hint);
                const ImVec2 hp(br.x - mt.pad - hs.x, br.y - mt.footer + U(2));
                const bool hintHovered = ImGui::IsMouseHoveringRect(hp, ImVec2(hp.x + hs.x, hp.y + hs.y));
                dl->AddText(font, fontSize, hp, ImGui::GetColorU32(hintHovered ? accent : kGrey), hint);
                ImGui::SetCursorScreenPos(hp);
                ImGui::InvisibleButton("##expand", hs);
                if (ImGui::IsItemClicked()) {
                    if (p.expanded) expanded_.erase(e.key);
                    else expanded_.insert(e.key);
                }
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            ImGui::PopID();
        }

        // give the scroll area its full height
        ImGui::SetCursorPos(ImVec2(origin.x, y));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
    }

    // ---- Generate Kin / Edit, Generate Ability / Edit --------------------------------------------------
    // GM tools, Kin and Abilities so far: write a custom card into one shared homebrew pack this app manages itself
    // (created the first time any of them is used), and let an existing card (a book one included) be overridden the
    // same way a homerule replaces a rule - the original file is never touched, only read; the edited card reads
    // "Changed by My Homebrew" from then on.

    static std::vector<std::string> splitBy(const std::string& text, char sep) {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find(sep, pos);
            if (end == std::string::npos) end = text.size();
            std::string piece = text.substr(pos, end - pos);
            while (!piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) piece.erase(piece.begin());
            while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\t' || piece.back() == '\r')) piece.pop_back();
            if (!piece.empty()) out.push_back(piece);
            pos = end + 1;
            if (end >= text.size()) break;
        }
        return out;
    }

    std::string customPackDir() const { return host_.paths().userPacksDir() + "/custom"; }

    // Copies a picked file into the pack's own images/ folder and returns the "images/<name>" reference for it.
    std::string copyIntoCustomPack(const std::string& absolutePath) {
        const std::string dir = customPackDir();
        SDL_CreateDirectory(dir.c_str());
        SDL_CreateDirectory((dir + "/images").c_str());
        const size_t slash = absolutePath.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? absolutePath : absolutePath.substr(slash + 1);
        if (const auto data = fs::readFile(absolutePath)) fs::writeFile(dir + "/images/" + base, *data);
        return "images/" + base;
    }

    // Appends `entry` to `<file>.json`'s own list (creating the pack, or that file within it, the first time), and
    // reloads. `file` is a data file name without ".json" ("kin", "abilities"...).
    bool saveCustomEntry(const char* file, json entry, std::string& error) {
        const std::string dir = customPackDir();
        SDL_CreateDirectory(dir.c_str());
        const std::string path = dir + "/" + file + ".json";
        json root = json::object();
        if (const auto text = fs::readFile(path)) {
            json parsed;
            if (jsonParse(*text, parsed, nullptr) && parsed.is_object()) root = parsed;
        }
        if (!root.contains("format")) root["format"] = 1;
        if (!root.contains("name")) root["name"] = "My Homebrew";
        if (!root.contains(file) || !root[file].is_array()) root[file] = json::array();
        root[file].push_back(std::move(entry));
        if (!fs::writeFile(path, root.dump(1))) {
            error = "Could not save: " + std::string(SDL_GetError());
            return false;
        }
        host_.contentChanged();
        return true;
    }

    // -- Kin --

    // `existing`: null for a new kin, the card being edited otherwise. Fills the form and opens the popup.
    void openKinForm(const Entry* existing) {
        kinOriginalKey_ = existing ? existing->key : "";
        kinName_ = existing ? existing->title : "";
        kinDescription_ = existing ? existing->body : "";
        kinMovement_ = existing ? existing->prop("movement") : "";
        kinAbilitiesSelected_.clear();
        if (existing)
            for (const std::string& a : existing->list("innate_abilities")) kinAbilitiesSelected_.insert(a);
        kinNames_.clear();
        if (existing)
            for (const std::string& n : existing->list("names")) kinNames_ += (kinNames_.empty() ? "" : "\n") + n;
        kinImagePicked_.clear();
        kinImageExisting_ = existing ? existing->image : "";
        kinError_.clear();
        kinNewAbilityOpen_ = false;
        kinWantOpen_ = true;
    }

    void drawKinForm() {
        if (kinWantOpen_) {
            ImGui::OpenPopup("Generate Kin");    // deferred to here: see kinWantOpen_
            kinWantOpen_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(U(520), 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Generate Kin", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextColored(kGrey, kinOriginalKey_.empty() ? "A new, custom kin - it goes on your own homebrew pack, nothing here is changed."
                                                          : "Your own version replaces this kin everywhere; the original is never touched.");
        ImGui::Spacing();
        ImGui::TextColored(kGold, "Name");
        inputStr("##kname", kinName_);
        ImGui::TextColored(kGold, "Movement");
        inputStr("##kmove", kinMovement_, U(100));
        ImGui::TextColored(kGold, "Description");
        inputMultiline("##kdesc", kinDescription_, ImVec2(-FLT_MIN, U(90)));

        ImGui::TextColored(kGold, "Innate abilities");
        ImGui::BeginChild("##kabilitylist", ImVec2(-FLT_MIN, U(120)), ImGuiChildFlags_Borders);
        for (const Entry& a : host_.content().entries(Kind::Ability)) {
            if (a.prop("type") != "kin") continue;   // only innate abilities are offered here - Kin never has a heroic one
            bool on = kinAbilitiesSelected_.count(a.title) != 0;
            if (ImGui::Checkbox((a.title + "##ka" + std::to_string(a.id)).c_str(), &on)) {
                if (on) kinAbilitiesSelected_.insert(a.title);
                else kinAbilitiesSelected_.erase(a.title);
            }
        }
        ImGui::EndChild();
        if (!kinNewAbilityOpen_) {
            if (ImGui::SmallButton("+ New innate ability…")) {
                kinNewAbilityOpen_ = true;
                newAbilityName_.clear();
                newAbilityWp_.clear();
                newAbilityDescription_.clear();
                newAbilityError_.clear();
            }
        } else {
            ImGui::Spacing();
            ImGui::TextColored(kGold, "New innate ability");
            ImGui::Text("Name");
            inputStr("##nabname", newAbilityName_);
            ImGui::Text("Willpower points");
            inputStr("##nabwp", newAbilityWp_, U(100));
            ImGui::Text("Description");
            inputMultiline("##nabdesc", newAbilityDescription_, ImVec2(-FLT_MIN, U(70)));
            if (!newAbilityError_.empty()) ImGui::TextColored(kRed, "%s", newAbilityError_.c_str());
            if (ImGui::SmallButton("Add")) {
                if (saveNewAbility()) {
                    kinAbilitiesSelected_.insert(newAbilityName_);
                    kinNewAbilityOpen_ = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel##nab")) kinNewAbilityOpen_ = false;
            ImGui::Spacing();
        }

        ImGui::TextColored(kGold, "First names (one per line)");
        inputMultiline("##knames", kinNames_, ImVec2(-FLT_MIN, U(90)));
        ImGui::TextColored(kGold, "Image");
        const std::string& preview = kinImagePicked_.empty() ? kinImageExisting_ : kinImagePicked_;
        if (!preview.empty())
            if (const TextureCache::Tex* t = host_.textures().get(preview)) {
                const float w = U(72), h = w * static_cast<float>(t->h) / static_cast<float>(t->w);
                ImGui::Image(reinterpret_cast<ImTextureID>(t->tex), ImVec2(w, h));
                ImGui::SameLine();
            }
        if (ImGui::Button("Choose image…")) kinImageDialog_.openFile(host_.window(), "Images", "png;jpg;jpeg;bmp;gif");
        std::string picked;
        if (kinImageDialog_.poll(picked) && !picked.empty()) kinImagePicked_ = picked;
        if (!kinError_.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kRed, "%s", kinError_.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Save")) {
            if (saveKinForm()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    bool saveKinForm() {
        if (kinName_.empty()) {
            kinError_ = "Name is required.";
            return false;
        }
        json entry = json::object();
        entry["name"] = kinName_;
        if (!kinOriginalKey_.empty()) entry["replaces"] = kinOriginalKey_;
        if (!kinDescription_.empty()) entry["description"] = kinDescription_;
        if (!kinMovement_.empty()) entry["movement"] = std::atoi(kinMovement_.c_str());
        if (!kinAbilitiesSelected_.empty()) entry["innate_abilities"] = std::vector<std::string>(kinAbilitiesSelected_.begin(), kinAbilitiesSelected_.end());
        const std::vector<std::string> names = splitBy(kinNames_, '\n');
        if (!names.empty()) entry["names"] = names;
        // only a newly chosen picture is written: a card that already has one (the book's own, or an earlier edit's) keeps
        // it as it is, exactly like leaving any other field untouched does
        if (!kinImagePicked_.empty()) entry["image"] = copyIntoCustomPack(kinImagePicked_);
        return saveCustomEntry("kin", std::move(entry), kinError_);
    }

    // -- Abilities --

    void openAbilityForm(const Entry* existing) {
        abilityOriginalKey_ = existing ? existing->key : "";
        abilityName_ = existing ? existing->title : "";
        abilityHeroic_ = !existing || existing->prop("type") != "kin";
        abilityRequirement_.clear();
        abilityWp_ = existing ? existing->prop("wp_cost") : "";
        abilityDescription_ = existing ? existing->body : "";
        if (existing)
            for (const Field& f : existing->fields)
                if (f.label == "Requirement") abilityRequirement_ = f.value;
        abilityError_.clear();
        abilityWantOpen_ = true;
    }

    void drawAbilityForm() {
        if (abilityWantOpen_) {
            ImGui::OpenPopup("Generate Ability");    // deferred to here: see kinWantOpen_
            abilityWantOpen_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(U(480), 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Generate Ability", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextColored(kGrey, abilityOriginalKey_.empty() ? "A new, custom ability - it goes on your own homebrew pack, nothing here is changed."
                                                              : "Your own version replaces this ability everywhere; the original is never touched.");
        ImGui::Spacing();
        ImGui::TextColored(kGold, "Name");
        inputStr("##aname", abilityName_);
        ImGui::TextColored(kGold, "Type");
        ImGui::RadioButton("Heroic", &abilityHeroic_, 1);   // wired through an int (1/0) - ImGui::RadioButton wants an int*, not a bool*
        ImGui::SameLine();
        ImGui::RadioButton("Innate", &abilityHeroic_, 0);
        ImGui::TextColored(kGold, "Requirement");
        inputStr("##areq", abilityRequirement_);
        ImGui::TextColored(kGold, "Willpower points");
        inputStr("##awp", abilityWp_, U(100));
        ImGui::TextColored(kGold, "Description");
        inputMultiline("##adesc", abilityDescription_, ImVec2(-FLT_MIN, U(120)));
        if (!abilityError_.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kRed, "%s", abilityError_.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Save")) {
            if (saveAbilityForm()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    bool saveAbilityForm() {
        if (abilityName_.empty()) {
            abilityError_ = "Name is required.";
            return false;
        }
        json entry = json::object();
        entry["name"] = abilityName_;
        if (!abilityOriginalKey_.empty()) entry["replaces"] = abilityOriginalKey_;
        entry["type"] = abilityHeroic_ ? "heroic" : "kin";
        if (!abilityRequirement_.empty()) entry["requirement"] = abilityRequirement_;
        if (!abilityWp_.empty()) entry["wp_cost"] = abilityWp_;
        if (!abilityDescription_.empty()) entry["description"] = abilityDescription_;
        return saveCustomEntry("abilities", std::move(entry), abilityError_);
    }

    // The Kin form's own quick "+ New innate ability…": the same ability data, minus a name clash with the outer form's
    // own state (it has no Requirement or Heroic/Innate choice - always Innate, that being the only kind Kin offers).
    bool saveNewAbility() {
        if (newAbilityName_.empty()) {
            newAbilityError_ = "Name is required.";
            return false;
        }
        json entry = json::object();
        entry["name"] = newAbilityName_;
        entry["type"] = "kin";
        if (!newAbilityWp_.empty()) entry["wp_cost"] = newAbilityWp_;
        if (!newAbilityDescription_.empty()) entry["description"] = newAbilityDescription_;
        return saveCustomEntry("abilities", std::move(entry), newAbilityError_);
    }

    std::string kinOriginalKey_, kinName_, kinDescription_, kinMovement_, kinNames_;
    std::set<std::string> kinAbilitiesSelected_;
    std::string kinImagePicked_, kinImageExisting_, kinError_;
    FileDialog kinImageDialog_;
    bool kinNewAbilityOpen_ = false;
    std::string newAbilityName_, newAbilityWp_, newAbilityDescription_, newAbilityError_;
    bool kinWantOpen_ = false;         // Edit is clicked from inside a per-card PushID scope, while the popup itself is
                                        // opened/drawn at the toolbar's ID level; OpenPopup and BeginPopupModal must see the
                                        // same ID stack or the popup silently never opens, so the actual OpenPopup() call is
                                        // deferred to drawKinForm(), matching where BeginPopupModal() runs.

    std::string abilityOriginalKey_, abilityName_, abilityRequirement_, abilityWp_, abilityDescription_, abilityError_;
    int abilityHeroic_ = 1;
    bool abilityWantOpen_ = false;     // see kinWantOpen_

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
    bool jumpToCards_ = false;                            // force the mosaic's own tab open once, right after onSelect() reaches a card
};

}  // namespace

std::unique_ptr<Module> makeCompendiumModule(Host& host, Compendium which) { return std::make_unique<CompendiumModule>(host, which); }

}  // namespace gm
