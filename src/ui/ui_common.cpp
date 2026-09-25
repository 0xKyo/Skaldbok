#include "ui/ui_common.h"

#include "parsing/fts.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>

namespace gm::ui {

// Palette: parchment-and-teal, echoing the books. Everything else is derived from ImGui's dark theme.
const ImVec4 kInk = ImVec4(0.200f, 0.161f, 0.122f, 1.0f);              // dark brown ink
const ImVec4 kAccent = ImVec4(0.137f, 0.412f, 0.353f, 1.0f);       // dragon green, for text on paper
const ImVec4 kAccentDim = ImVec4(0.184f, 0.478f, 0.408f, 1.0f);
const ImVec4 kGrey = ImVec4(0.490f, 0.424f, 0.329f, 1.0f);           // faded ink
const ImVec4 kGold = ImVec4(0.478f, 0.294f, 0.165f, 1.0f);           // the brown of the sheet's small labels
const ImVec4 kRed = ImVec4(0.745f, 0.180f, 0.149f, 1.0f);
const ImVec4 kRoll = ImVec4(0.902f, 0.776f, 0.416f, 0.55f);         // the yellow of the parchment, for the rolled row

float lineH() { return ImGui::GetTextLineHeight(); }
float U(float v) { return v * ImGui::GetFontSize() / 16.0f; }

void bigText(const char* text, float scale, ImVec4 color) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

std::string lowered(std::string s) { return lowerCopy(std::move(s)); }

ImVec4 sourceColor(const ContentStore& content, int sourceId) {
    const SourceInfo* s = content.source(sourceId);
    if (!s) return kGrey;
    constexpr float kOnPaper = 0.62f;                     // the badge colours are bright: darkened so they read on cream paper
    return ImVec4(static_cast<float>((s->color >> 16) & 0xFF) / 255.0f * kOnPaper, static_cast<float>((s->color >> 8) & 0xFF) / 255.0f * kOnPaper,
                  static_cast<float>(s->color & 0xFF) / 255.0f * kOnPaper, 1.0f);
}

ImVec4 tagColor(size_t index) {
    static const ImVec4 palette[] = {
        ImVec4(0.184f, 0.478f, 0.408f, 1.0f),  // green
        ImVec4(0.710f, 0.520f, 0.100f, 1.0f),  // gold
        ImVec4(0.478f, 0.333f, 0.690f, 1.0f),  // violet
        ImVec4(0.769f, 0.416f, 0.133f, 1.0f),  // orange
        ImVec4(0.235f, 0.431f, 0.710f, 1.0f),  // blue
        ImVec4(0.690f, 0.290f, 0.416f, 1.0f),  // rose
    };
    return palette[index % (sizeof palette / sizeof palette[0])];
}

// ---------------------------------------------------------------------------------- building blocks

void sourceBadge(Host& host, int sourceId) {
    const SourceInfo* s = host.content().source(sourceId);
    ImGui::PushStyleColor(ImGuiCol_Text, sourceColor(host.content(), sourceId));
    ImGui::TextUnformatted(s ? s->label.c_str() : "?");
    ImGui::PopStyleColor();
}

// A small, non-clickable note of where this comes from in the original book ("Rulebook p.18"): the app never ships or
// opens the books' own PDFs, so this is a footnote, not a link.
void pageLink(Host& host, int sourceId, const PageRef& r, const std::string& pageNote) {
    const SourceInfo* src = host.content().source(sourceId);
    if (!r.valid()) {
        if (src && src->homebrew) {
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.85f);
            ImGui::TextColored(kGrey, "%s%s%s", src->label.c_str(), pageNote.empty() ? "" : " · ", pageNote.c_str());
            ImGui::PopFont();
        }
        return;
    }
    char label[96];
    const char* name = src ? src->label.c_str() : "?";
    if (r.printed > 0) std::snprintf(label, sizeof label, "Original: %s p.%d", name, r.printed);
    else std::snprintf(label, sizeof label, "Original: %s (pdf p.%d)", name, r.page);
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.85f);
    ImGui::TextColored(kGrey, "%s", label);
    ImGui::PopFont();
}

void detailHeader(Host& host, const std::string& title, const std::string& subtitle, int sourceId, const Selection& shown) {
    const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    bigText(title.c_str(), 1.7f, ImGui::GetStyle().Colors[ImGuiCol_Text]);
    if (IMasterScreen* screen = serviceOf<IMasterScreen>(host)) {     // pins the entry shown here to the Master Screen
        const float btnW = ImGui::CalcTextSize("Pin").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(rightEdge - btnW);
        if (ImGui::SmallButton("Pin")) screen->pinEntry(shown.kind, shown.id);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pin this to the Master Screen");
    }
    sourceBadge(host, sourceId);
    if (!subtitle.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "· %s", subtitle.c_str());
    }
    ImGui::Separator();
}

// InputTextMultiline only breaks lines at an explicit '\n' - unlike TextWrapped, it never reflows a long line by
// itself, so without this a word run past the edge would just be cut off instead of wrapping. This inserts the same
// breaks TextWrapped would have chosen (the font's own word-wrap position), so each resulting line already fits.
static std::string wrapToWidth(const std::string& text, float wrapWidth) {
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    std::string out;
    out.reserve(text.size() + 8);
    size_t paraStart = 0;
    while (paraStart <= text.size()) {
        size_t paraEnd = text.find('\n', paraStart);
        if (paraEnd == std::string::npos) paraEnd = text.size();
        const char* p = text.c_str() + paraStart;
        const char* segEnd = text.c_str() + paraEnd;
        while (p < segEnd) {
            const char* wrapEnd = font->CalcWordWrapPosition(fontSize, p, segEnd, wrapWidth);
            if (wrapEnd <= p) wrapEnd = p + 1;    // an unbreakable run wider than wrapWidth: take at least one char, never stall
            out.append(p, wrapEnd);
            p = wrapEnd;
            while (p < segEnd && *p == ' ') ++p;  // the space that caused the wrap is dropped, like TextWrapped drops it
            if (p < segEnd) out += '\n';
        }
        if (paraEnd >= text.size()) break;
        out += '\n';
        paraStart = paraEnd + 1;
    }
    return out;
}

void copyableText(const char* id, const std::string& text, float wrapWidth) {
    if (text.empty()) return;
    const float wrap = wrapWidth > 0 ? wrapWidth : std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const std::string wrapped = wrapToWidth(text, wrap);
    const float height = ImGui::CalcTextSize(wrapped.c_str()).y;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    // InputTextMultiline needs a writable buffer, but ReadOnly means it is only ever read from.
    std::vector<char> buf(wrapped.begin(), wrapped.end());
    buf.push_back('\0');
    ImGui::InputTextMultiline(id, buf.data(), buf.size(), ImVec2(wrap, height + 1),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

namespace {
bool g_pageFocused = false;
bool g_keyboardInUse = false;
}  // namespace

void setPageKeyboard(bool pageFocused, bool keyboardInUse) {
    g_pageFocused = pageFocused;
    g_keyboardInUse = keyboardInUse;
}

bool pageFocused() { return g_pageFocused; }

bool pageKeys() { return g_pageFocused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive(); }

void keyScroll() {
    if (!pageKeys()) return;
    const float line = lineH() * 3, page = ImGui::GetWindowHeight() * 0.9f;
    float dy = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = line;
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -line;
    else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) dy = page;
    else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) dy = -page;
    else if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) dy = -ImGui::GetScrollMaxY();
    else if (ImGui::IsKeyPressed(ImGuiKey_End, false)) dy = ImGui::GetScrollMaxY();
    if (dy != 0) ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() + dy, 0.0f, ImGui::GetScrollMaxY()));
}

void focusFrame() {
    if (!g_keyboardInUse) return;
    const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + s.x, p.y + s.y), false);
    dl->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + s.x - 1, p.y + s.y - 1), ImGui::GetColorU32(kAccent),
                ImGui::GetStyle().ChildRounding, 2.0f);
    dl->PopClipRect();
}

void introPage(const Intro& intro, const char* id) {
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);           // the arrows scroll the text (keyScroll), not ImGui's nav
    ImGui::BeginChild(id, ImVec2(0, 0));
    keyScroll();
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
    ImGui::PopItemFlag();
}

void introTabs(IntroTabs& tabs, const char* id, const char* pageLabel, const Intro& intro, const std::function<void()>& page) {
    if (intro.empty()) {
        page();
        return;
    }
    if (!ImGui::BeginTabBar(id)) return;
    if (pageKeys()) {
        if (tabs.showingIntro && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) tabs.wantPage = true;
        if (!tabs.showingIntro && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) tabs.wantIntro = true;
    }
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool introOpen = ImGui::BeginTabItem("Intro", nullptr, tabs.wantIntro ? ImGuiTabItemFlags_SetSelected : 0);
    ImGui::PopItemFlag();
    tabs.showingIntro = introOpen;
    if (introOpen) {
        introPage(intro, "##intro");
        ImGui::EndTabItem();
    }
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool pageOpen = ImGui::BeginTabItem(pageLabel, nullptr, tabs.wantPage ? ImGuiTabItemFlags_SetSelected : 0);
    ImGui::PopItemFlag();
    tabs.wantIntro = tabs.wantPage = false;
    if (pageOpen) {
        page();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void paragraphs(const std::string& text, const char* id) {
    ImGui::PushID(id);
    size_t pos = 0;
    int line = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string l = text.substr(pos, end - pos);
        pos = end + 1;
        if (l.empty()) {
            ImGui::Dummy(ImVec2(0, 4));
            continue;
        }
        const std::string lineId = "##l" + std::to_string(line++);
        const size_t colon = l.find(':');
        size_t digits = 0;
        while (digits < l.size() && std::isdigit(static_cast<unsigned char>(l[digits]))) ++digits;
        const bool ordered = digits > 0 && digits + 1 < l.size() && l[digits] == '.' && l[digits + 1] == ' ';
        if (l.starts_with("\xE2\x9C\xA6") && colon != std::string::npos && colon < 44) {
            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            ImGui::TextUnformatted(l.substr(0, colon + 1).c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 5);
            copyableText(lineId.c_str(), l.substr(colon + 1));
        } else if (l.starts_with("- ") || l.starts_with("* ")) {          // "- Item text" -> a bulleted line
            ImGui::TextUnformatted("\xE2\x80\xA2");                       // "•"
            ImGui::SameLine(0, 8);
            copyableText(lineId.c_str(), l.substr(2));
        } else if (ordered) {                                             // "3. Item text" -> a numbered line, kept as written
            ImGui::TextUnformatted(l.substr(0, digits + 1).c_str());
            ImGui::SameLine(0, 8);
            copyableText(lineId.c_str(), l.substr(digits + 2));
        } else {
            copyableText(lineId.c_str(), l);
        }
        ImGui::Dummy(ImVec2(0, 2));
        if (end >= text.size()) break;
    }
    ImGui::PopID();
}

void highlighted(const std::string& s) {
    const float wrapW = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lh = lineH() + 1;
    const float space = ImGui::CalcTextSize(" ").x;
    const ImU32 normal = ImGui::GetColorU32(kGrey);
    const ImU32 hit = ImGui::GetColorU32(kGold);
    float x = 0, y = 0;
    bool hl = false;
    std::vector<std::pair<std::string, bool>> segs;
    std::string cur;
    auto closeSeg = [&] {
        if (!cur.empty()) segs.emplace_back(cur, hl);
        cur.clear();
    };
    auto flushWord = [&] {
        closeSeg();
        if (segs.empty()) return;
        float w = 0;
        for (auto& sg : segs) w += ImGui::CalcTextSize(sg.first.c_str()).x;
        if (x > 0 && x + w > wrapW) {
            x = 0;
            y += lh;
        }
        for (auto& sg : segs) {
            dl->AddText(ImVec2(origin.x + x, origin.y + y), sg.second ? hit : normal, sg.first.c_str());
            x += ImGui::CalcTextSize(sg.first.c_str()).x;
        }
        x += space;
        segs.clear();
    };
    for (char c : s) {
        if (c == '\x01') {
            closeSeg();
            hl = true;
        } else if (c == '\x02') {
            closeSeg();
            hl = false;
        } else if (c == ' ' || c == '\n') {
            flushWord();
        } else {
            cur += c;
        }
    }
    flushWord();
    ImGui::Dummy(ImVec2(wrapW, y + lh));
}

void fieldsTable(const std::vector<Field>& fields, const char* id) {
    if (fields.empty()) return;
    ImGui::PushID(id);
    if (!ImGui::BeginTable("##t", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::PopID();
        return;
    }
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, U(150.0f));
    ImGui::TableSetupColumn("v");
    for (size_t i = 0; i < fields.size(); ++i) {
        const Field& f = fields[i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(kGold, "%s", f.label.c_str());
        ImGui::TableSetColumnIndex(1);
        copyableText(("##v" + std::to_string(i)).c_str(), f.value);
    }
    ImGui::EndTable();
    ImGui::PopID();
}

void tableGrid(const DataTable& t, int highlightRow) {
    const bool hasRoll = !t.dice.empty();
    const int cols = static_cast<int>(t.columns.size()) + (hasRoll ? 1 : 0);
    if (cols == 0) return;
    // Short columns ("Uncommon", "2D8", "12 gold") get exactly the width their widest cell needs so they never wrap
    // mid-word; long text columns share whatever is left in proportion to their content.
    const float pad = ImGui::GetStyle().CellPadding.x * 2.0f + U(4.0f);
    struct Col {
        float width;
        bool fixed;
    };
    std::vector<Col> layout;
    float need = hasRoll ? U(56.0f) : 0.0f;
    for (size_t c = 0; c < t.columns.size(); ++c) {
        float widest = ImGui::CalcTextSize(t.columns[c].c_str()).x;
        for (const TableRow& r : t.rows)
            if (c < r.cells.size()) widest = std::max(widest, ImGui::CalcTextSize(r.cells[c].c_str()).x);
        if (widest <= U(150.0f)) {
            layout.push_back({widest + U(2.0f), true});     // ImGui adds the cell padding itself
            need += widest + pad;
        } else {
            layout.push_back({std::min(widest, U(600.0f)), false});
            need += U(190.0f) + pad;                         // a text column is never squeezed below this
        }
    }
    // Stretch columns need a defined width; only scroll sideways when the table truly does not fit. The sideways scroll belongs to a box
    // that is as tall as the table (a table's own ScrollX would be as tall as the room left in the window: several tables one below the
    // other, as in a rule's section, would be cut off).
    const bool scroll = need > ImGui::GetContentRegionAvail().x;
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (scroll && !ImGui::BeginChild("##gridbox", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    const bool shown = ImGui::BeginTable("##grid", cols, flags, ImVec2(scroll ? need : 0.0f, 0));
    if (!shown) {
        if (scroll) ImGui::EndChild();
        return;
    }
    if (hasRoll) ImGui::TableSetupColumn(t.dice.c_str(), ImGuiTableColumnFlags_WidthFixed, U(56.0f));
    for (size_t c = 0; c < t.columns.size(); ++c)
        ImGui::TableSetupColumn(t.columns[c].c_str(), layout[c].fixed ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch,
                                layout[c].width);
    ImGui::TableHeadersRow();
    for (size_t r = 0; r < t.rows.size(); ++r) {
        const TableRow& row = t.rows[r];
        ImGui::TableNextRow();
        if (static_cast<int>(r) == highlightRow) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(kRoll));
        int col = 0;
        if (hasRoll) {
            ImGui::TableSetColumnIndex(col++);
            ImGui::TextColored(kGold, "%s", row.rollText.c_str());
        }
        for (size_t c = 0; c < row.cells.size(); ++c) {
            if (col >= cols) break;
            ImGui::TableSetColumnIndex(col++);
            copyableText(("##c" + std::to_string(r) + "_" + std::to_string(c)).c_str(), row.cells[c]);
        }
    }
    ImGui::EndTable();
    if (scroll) ImGui::EndChild();
}

void attacksTable(Host& host, const Monster& m, int& rolledRow, int& rolledValue) {
    if (m.attacks.empty()) return;
    ImGui::PushID("attacks");
    ImGui::Spacing();
    const std::string dieName = m.attackDice.empty() ? "D6" : m.attackDice;
    bigText(("Monster attacks · " + dieName).c_str(), 1.15f, kAccent);
    const int sides = std::max(2, std::atoi(dieName.c_str() + 1));
    if (ImGui::Button(("Roll " + dieName + " attack").c_str())) {
        rolledValue = host.dice().roll(sides);
        rolledRow = -1;
        for (size_t i = 0; i < m.attacks.size(); ++i)
            if (rolledValue >= m.attacks[i].rollMin && rolledValue <= m.attacks[i].rollMax) rolledRow = static_cast<int>(i);
        host.flashRoll();
    }
    if (rolledRow >= 0) {
        ImGui::SameLine();
        ImGui::TextColored(kGold, "Rolled %d", rolledValue);
    }
    if (ImGui::BeginTable("##atk", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(dieName.c_str(), ImGuiTableColumnFlags_WidthFixed, U(56.0f));
        ImGui::TableSetupColumn("ATTACK");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < m.attacks.size(); ++i) {
            const Attack& a = m.attacks[i];
            ImGui::TableNextRow();
            if (static_cast<int>(i) == rolledRow) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(kRoll));
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kGold, "%s", a.rollText.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string rest = a.text;
            if (!a.name.empty() && rest.starts_with(a.name)) {
                ImGui::TextColored(kAccent, "%s", a.name.c_str());
                ImGui::SameLine(0, 5);
                rest = rest.substr(a.name.size());
            }
            copyableText(("##a" + std::to_string(i)).c_str(), rest);
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

// ------------------------------------------------------------------------------------------ ListView

void ListView::set(std::vector<ListItem> items) {
    all = std::move(items);
    appliedRevision_ = -1;                 // force a refilter on the next draw
    shown.clear();
    for (size_t i = 0; i < all.size(); ++i) shown.push_back(static_cast<int>(i));
}

void ListView::refilter(Host& host) {
    appliedFilter_ = filter;
    appliedRevision_ = host.sourceRevision();
    const std::string needle = lowered(filter);
    shown.clear();
    for (size_t i = 0; i < all.size(); ++i) {
        const ListItem& it = all[i];
        if (!host.sourceShown(it.sourceId)) continue;
        if (!needle.empty() && !lowered(it.name).contains(needle) && !lowered(it.sub).contains(needle)) continue;
        shown.push_back(static_cast<int>(i));
    }
}

void ListView::ensureVisible(Host& host, const Selection& s) {
    scrollToSelection = true;
    for (int i : shown)
        if (all[static_cast<size_t>(i)].id == s.id && all[static_cast<size_t>(i)].kind == s.kind) return;
    filter[0] = 0;                         // the item is hidden by a filter: clear it so the jump is visible
    refilter(host);
}

const ListItem* ListView::step(const Selection* selected, int delta) const {
    if (shown.empty()) return nullptr;
    int cur = -1;
    for (size_t i = 0; i < shown.size(); ++i) {
        const ListItem& it = all[static_cast<size_t>(shown[i])];
        if (selected && it.kind == selected->kind && it.id == selected->id) cur = static_cast<int>(i);
    }
    const int last = static_cast<int>(shown.size()) - 1;
    const int next = std::clamp(cur < 0 ? (delta > 0 ? 0 : last) : cur + delta, 0, last);
    return &all[static_cast<size_t>(shown[static_cast<size_t>(next)])];
}

const ListItem* ListView::draw(Host& host, const char* hint, const Selection* selected) {
    const ListItem* picked = nullptr;
    const ImGuiIO& io = ImGui::GetIO();
    // The list has the keyboard while the page does (a Tab from the nav rail focuses the page, not a row). Nothing
    // here is reachable by ImGui's own nav, so the arrows are the list's alone; the filter only through Ctrl+F.
    const bool keys = pageKeys();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) focusFilter_ = true;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, !focusFilter_ && !filterActive_);
    if (focusFilter_) ImGui::SetKeyboardFocusHere();
    focusFilter_ = false;
    ImGui::InputTextWithHint("##filter", hint, filter, sizeof filter);
    ImGui::PopItemFlag();
    filterActive_ = ImGui::IsItemActive();
    if (filterActive_ && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                          ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))) {
        ImGui::SetWindowFocus(nullptr);                  // from the filter down to the rows: drop its text focus...
        ImGui::SetWindowFocus();                         // ...and keep the panel's
    }
    if (appliedFilter_ != filter || appliedRevision_ != host.sourceRevision()) refilter(host);
    ImGui::TextColored(kGrey, "%d of %d", static_cast<int>(shown.size()), static_cast<int>(all.size()));

    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::BeginChild("##items", ImVec2(0, 0), ImGuiChildFlags_None);
    // arrow keys move through the list as soon as the page has focus; each press changes the selection immediately
    if (keys && !filterActive_) {
        int delta = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) delta = 1;
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) delta = -1;
        else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) delta = 10;
        else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) delta = -10;
        else if (ImGui::IsKeyPressed(ImGuiKey_Home)) delta = -1000000;
        else if (ImGui::IsKeyPressed(ImGuiKey_End)) delta = 1000000;
        if (delta) picked = step(selected, delta);
    }
    const float rowH = lineH() + 10.0f;
    const float stride = rowH + ImGui::GetStyle().ItemSpacing.y;
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(shown.size()), stride);
    if (scrollToSelection) {
        for (size_t i = 0; i < shown.size(); ++i) {
            const ListItem& it = all[static_cast<size_t>(shown[i])];
            if (selected && it.id == selected->id && it.kind == selected->kind) {
                ImGui::SetScrollY(std::max(0.0f, static_cast<float>(i) * stride - ImGui::GetWindowHeight() * 0.35f));
                break;
            }
        }
        scrollToSelection = false;
    }
    while (clip.Step()) {
        for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
            const ListItem& it = all[static_cast<size_t>(shown[static_cast<size_t>(row)])];
            const bool isSel = selected && selected->kind == it.kind && selected->id == it.id;
            ImGui::PushID(row);
            if (ImGui::Selectable("##row", isSel, ImGuiSelectableFlags_None, ImVec2(0, rowH))) picked = &it;
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float cy = (mn.y + mx.y) * 0.5f - lineH() * 0.5f;
            dl->AddRectFilled(ImVec2(mn.x + 2, mn.y + 5), ImVec2(mn.x + 6, mx.y - 5), ImGui::GetColorU32(sourceColor(host.content(), it.sourceId)), 2.0f);
            dl->AddText(ImVec2(mn.x + 14, cy), ImGui::GetColorU32(ImGuiCol_Text), it.name.c_str());
            if (!it.sub.empty()) {
                const ImVec2 sz = ImGui::CalcTextSize(it.sub.c_str());
                const float nameW = ImGui::CalcTextSize(it.name.c_str()).x;
                if (mn.x + 14 + nameW + 12 + sz.x < mx.x) dl->AddText(ImVec2(mx.x - sz.x - 8, cy), ImGui::GetColorU32(kGrey), it.sub.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopItemFlag();
    return picked;
}

// -------------------------------------------------------------------------------------------- backdrop

void Backdrop::begin(float width, ImU32 fill, ImU32 edge, float pad, float rounding) {
    dl_ = ImGui::GetWindowDrawList();
    origin_ = ImGui::GetCursorScreenPos();
    width_ = width;
    fill_ = fill;
    edge_ = edge;
    pad_ = pad;
    rounding_ = rounding;
    dl_->ChannelsSplit(2);
    dl_->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(origin_.x + pad, origin_.y + pad));
    ImGui::BeginGroup();
}

void Backdrop::end() {
    ImGui::EndGroup();
    const ImVec2 max(origin_.x + width_, ImGui::GetItemRectMax().y + pad_);
    dl_->ChannelsSetCurrent(0);
    dl_->AddRectFilled(origin_, max, fill_, U(rounding_));
    dl_->AddRect(origin_, max, edge_, U(rounding_), U(1.5f));
    dl_->ChannelsMerge();
    ImGui::SetCursorScreenPos(ImVec2(origin_.x, max.y));
    ImGui::Dummy(ImVec2(width_, U(8)));
}

// ---------------------------------------------------------------------------------------------- inputs

namespace {
// InputText grows the std::string itself through this callback, so there is no fixed-size buffer to copy in and out.
int growString(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(d->UserData);
        s->resize(static_cast<size_t>(d->BufTextLen));
        d->Buf = s->data();
    }
    return 0;
}
}  // namespace

bool inputStr(const char* id, std::string& s, float width, const char* hint) {
    ImGui::SetNextItemWidth(width);
    constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    return hint ? ImGui::InputTextWithHint(id, hint, s.data(), s.capacity() + 1, flags, growString, &s)
                : ImGui::InputText(id, s.data(), s.capacity() + 1, flags, growString, &s);
}

bool inputMultiline(const char* id, std::string& s, ImVec2 size) {
    return ImGui::InputTextMultiline(id, s.data(), s.capacity() + 1, size, ImGuiInputTextFlags_CallbackResize, growString, &s);
}

bool smallInt(const char* id, int& v, int lo, int hi, float width) {
    ImGui::SetNextItemWidth(U(width));
    const bool changed = ImGui::InputInt(id, &v, 0, 0);
    if (changed) v = std::clamp(v, lo, hi);
    return changed;
}

std::string capitalized(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

const Entry* entryFor(const ContentStore& cs, Kind kind, const Ref& r) {
    if (!r.key.empty())
        if (const Entry* e = cs.entry(kind, cs.idByKey(kind, r.key))) return e;
    return r.name.empty() ? nullptr : cs.findByName(kind, r.name);
}

void entryTooltip(const Entry* e) {
    if (!e || !ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextColored(kAccent, "%s", e->title.c_str());
    ImGui::TextColored(kGrey, "%s", e->subtitle.c_str());
    for (const Field& f : e->fields) ImGui::Text("%s: %s", f.label.c_str(), f.value.c_str());
    if (!e->body.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(e->body.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool pickEntry(const ContentStore& cs, const char* popup, std::initializer_list<Kind> kinds, const Entry*& out) {
    static char filter[64];
    bool picked = false;
    ImGui::SetNextWindowSize(ImVec2(U(420), U(340)), ImGuiCond_Appearing);
    if (ImGui::BeginPopup(popup)) {
        if (ImGui::IsWindowAppearing()) {
            filter[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##pickfilter", "Filter…", filter, sizeof filter);
        const std::string needle = lowered(filter);
        ImGui::BeginChild("##picklist", ImVec2(0, 0));
        for (Kind k : kinds)
            for (const Entry& e : cs.entries(k)) {
                if (!needle.empty() && !lowered(e.title + " " + e.subtitle).contains(needle)) continue;
                ImGui::PushID(e.key.c_str());
                if (ImGui::Selectable(e.title.c_str())) {
                    out = &e;
                    picked = true;
                    ImGui::CloseCurrentPopup();
                }
                entryTooltip(&e);
                ImGui::SameLine();
                ImGui::TextColored(kGrey, "%s", e.subtitle.c_str());
                ImGui::PopID();
            }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return picked;
}

}  // namespace gm::ui
