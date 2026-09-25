// The "Intro" page of a category (a data file's top-level "intro"): the book's own chapter text above the list, and an editor for it.
// The text is edited in place and written back to the JSON it came from (data/system/<file>.json for Core), so the file stays the one
// source of truth. Tables are shown but not edited here: they are kept exactly as they are in the file.
#include <algorithm>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "parsing/fsutil.h"
#include "parsing/jsonutil.h"
#include "ui/ui_common.h"

namespace gm::ui {

namespace {

// A link to a section: the page shows it the next time its intro is drawn.
struct PendingSection {
    Kind kind = Kind::Spell;
    int section = -1;
    int line = -1;                     // the line of the section the link is on
} g_pending;

// Where the intro of `kind` lives: the file it was read from or, when the kind has none yet, Core's system file for it.
std::string introFile(Host& host, Kind kind) {
    const Intro& intro = host.content().introOf(kind);
    if (!intro.file.empty()) return intro.file;
    return host.paths().dataDir + "/system/" + kindFile(kind) + ".json";
}

// A body of text with the intro's tables in it where it asks for them; `used` remembers which ones were placed.
void drawBody(const Intro& intro, const std::string& text, const std::string& id, std::vector<bool>& used) {
    std::string chunk;
    int part = 0, lineNo = 0, chunkStart = 0;
    auto flush = [&] {
        if (!chunk.empty()) paragraphs(chunk, (id + "#" + std::to_string(part++)).c_str(), id.c_str(), chunkStart);
        chunk.clear();
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        std::string name;
        int which = -1;
        if (tableMarker(line, name))
            for (size_t i = 0; i < intro.tables.size(); ++i)
                if (!used[i] && lowered(intro.tables[i].title) == lowered(name)) {
                    which = static_cast<int>(i);
                    break;
                }
        if (which >= 0) {
            flush();
            used[static_cast<size_t>(which)] = true;
            ImGui::PushID(which);
            ImGui::Spacing();
            bigText(intro.tables[static_cast<size_t>(which)].title.c_str(), 1.1f, kAccent);
            tableGrid(intro.tables[static_cast<size_t>(which)], -1);
            ImGui::PopID();
        } else {
            if (chunk.empty()) chunkStart = lineNo;
            chunk += (chunk.empty() ? "" : "\n") + line;
        }
        ++lineNo;
        if (end >= text.size()) break;
    }
    flush();
}

void beginEdit(Host& host, Kind kind, IntroEdit& edit) {
    const Intro& intro = host.content().introOf(kind);
    edit.on = true;
    edit.error.clear();
    edit.body = intro.body;
    edit.sections.clear();
    for (const RuleNode::Section& s : intro.sections) edit.sections.push_back({s.title, s.body});
    // the tables come from the file itself (not from the loaded ones), so nothing the editor has no field for is lost
    edit.tables.clear();
    edit.openTable = -1;
    const auto text = fs::readFile(introFile(host, kind));
    json root;
    if (!text || !jsonParse(*text, root, nullptr) || !root.is_object()) return;
    const json* in = jsonFind(root, "intro");
    // the text as written in the file: the loaded one has its {{key: word}} markers taken out
    if (in && in->is_string()) {
        edit.body = in->get<std::string>();
    } else if (in && in->is_object()) {
        edit.body = jsonStr(*in, "body");
        if (const json* secs = jsonFind(*in, "sections"); secs && secs->is_array()) {
            edit.sections.clear();
            for (const json& sec : *secs)
                if (sec.is_object()) edit.sections.push_back({jsonStr(sec, "name"), jsonText(sec.contains("body") ? sec["body"] : json())});
        }
    }
    const json* tables = in && in->is_object() ? jsonFind(*in, "tables") : nullptr;
    if (!tables || !tables->is_array()) return;
    for (const json& t : *tables) {
        if (!t.is_object()) continue;
        IntroEdit::TableEdit te;
        te.name = jsonStr(t, "name");
        te.dice = jsonStr(t, "dice");
        te.columns = jsonStrings(t, "columns");
        if (const json* rows = jsonFind(t, "rows"); rows && rows->is_array())
            for (const json& r : *rows) {
                IntroEdit::TableEdit::Row row;
                if (r.is_object()) {
                    row.cells = jsonStrings(r, "cells");
                    row.roll = jsonStr(r, "roll");
                } else if (r.is_array()) {
                    for (const json& c : r) row.cells.push_back(jsonText(c));
                } else {
                    row.cells.push_back(jsonText(r));
                }
                te.rows.push_back(std::move(row));
            }
        json rest = t;
        for (const char* k : {"name", "dice", "columns", "rows"}) rest.erase(k);
        if (!rest.empty()) te.extra = rest.dump();
        edit.tables.push_back(std::move(te));
    }
}

std::string trimmedText(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") + 1 - a);
}

// Writes the fields into the file's "intro", leaving everything else in the file (and the intro's own tables) as it was.
bool saveEdit(Host& host, Kind kind, IntroEdit& edit) {
    const std::string path = introFile(host, kind);
    json root = json::object();
    if (const auto text = fs::readFile(path)) {
        json parsed;
        std::string err;
        if (!jsonParse(*text, parsed, &err) || !parsed.is_object()) {
            edit.error = "Not saved: " + path + " is not a JSON object (" + err + ")";
            return false;
        }
        root = std::move(parsed);
    }
    if (!root.contains("format")) root["format"] = 1;

    json tables;                                                    // null when there are none
    for (const IntroEdit::TableEdit& te : edit.tables) {
        if (te.columns.empty()) continue;
        json o = json::object();
        if (!te.extra.empty()) jsonParse(te.extra, o, nullptr);
        if (!o.is_object()) o = json::object();
        o["name"] = trimmedText(te.name).empty() ? "Table" : trimmedText(te.name);
        const std::string dice = trimmedText(te.dice);
        if (dice.empty()) o.erase("dice");
        else o["dice"] = dice;
        o["columns"] = te.columns;
        json rows = json::array();
        for (const IntroEdit::TableEdit::Row& r : te.rows) {
            json row = json::object();
            std::vector<std::string> cells = r.cells;
            cells.resize(te.columns.size());
            row["cells"] = cells;
            if (!dice.empty() && !trimmedText(r.roll).empty()) row["roll"] = trimmedText(r.roll);
            rows.push_back(row);
        }
        o["rows"] = rows;
        if (tables.is_null()) tables = json::array();
        tables.push_back(o);
    }

    json sections = json::array();
    for (const IntroEdit::Section& s : edit.sections) {
        const std::string title = trimmedText(s.title), body = trimmedText(s.body);
        if (title.empty() && body.empty()) continue;
        sections.push_back({{"name", title.empty() ? "Section" : title}, {"body", body}});
    }
    const std::string body = trimmedText(edit.body);
    if (body.empty() && sections.empty() && tables.is_null()) {
        root.erase("intro");
    } else if (sections.empty() && tables.is_null()) {
        root["intro"] = body;                                       // the plain form: "intro": "text"
    } else {
        json in = json::object();
        if (!body.empty()) in["body"] = body;
        if (!sections.empty()) in["sections"] = sections;
        if (!tables.is_null()) in["tables"] = tables;
        root["intro"] = in;
    }
    if (!fs::writeFile(path, root.dump(1) + "\n")) {
        edit.error = "Could not save " + path;
        return false;
    }
    edit.on = false;
    edit.error.clear();
    host.contentChanged();                                           // reloads: the page shows what the file now says
    return true;
}

// A word-wrapped text box as tall as its text (at least `minLines`, at most `maxLines`), so a long body is all in view.
void textBox(const char* id, std::string& text, int minLines, int maxLines) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float inner = std::max(1.0f, ImGui::GetContentRegionAvail().x - style.FramePadding.x * 2 - style.ScrollbarSize);
    const float textH = ImGui::CalcTextSize(text.c_str(), nullptr, false, inner).y;
    const float h = std::clamp(textH + lineH(), lineH() * static_cast<float>(minLines), lineH() * static_cast<float>(maxLines));
    inputMultiline(id, text, ImVec2(-FLT_MIN, h + style.FramePadding.y * 2), ImGuiInputTextFlags_WordWrap);
}

// The tables of the page: each one a header that opens into its name, its dice, a grid of cells and the buttons that change it.
void drawTables(IntroEdit& edit) {
    ImGui::Separator();
    ImGui::TextColored(kGold, "Tables");
    int removeTable = -1;
    for (size_t i = 0; i < edit.tables.size(); ++i) {
        IntroEdit::TableEdit& t = edit.tables[i];
        ImGui::PushID(static_cast<int>(i) + 1000);
        if (edit.openTable == static_cast<int>(i)) {
            ImGui::SetNextItemOpen(true);
            edit.openTable = -1;
        }
        const std::string head = (t.name.empty() ? std::string("(table without a name)") : t.name) + "###head";
        if (ImGui::CollapsingHeader(head.c_str())) {
            inputStr("##name", t.name, U(280), "Table name");
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "Dice");
            ImGui::SameLine();
            inputStr("##dice", t.dice, U(70), "D20");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leave empty for a plain table; \"D20\", \"D6\"... adds a Roll column");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove table")) removeTable = static_cast<int>(i);

            const bool dice = !t.dice.empty();
            const int extra = dice ? 1 : 0, cols = static_cast<int>(t.columns.size());
            int removeCol = -1, removeRow = -1;
            if (cols > 0 && ImGui::BeginTable("##grid", cols + extra + 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
                if (dice) ImGui::TableSetupColumn("##roll", ImGuiTableColumnFlags_WidthFixed, U(70));
                for (int c = 0; c < cols; ++c) ImGui::TableSetupColumn(("##c" + std::to_string(c)).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, U(30));
                ImGui::TableNextRow();                                     // the column names
                int col = 0;
                if (dice) {
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::TextColored(kGrey, "Roll");
                }
                for (int c = 0; c < cols; ++c) {
                    ImGui::TableSetColumnIndex(col++);
                    ImGui::PushID(c);
                    inputStr("##h", t.columns[static_cast<size_t>(c)], -FLT_MIN, "Column");
                    if (cols > 1 && ImGui::SmallButton("Remove column")) removeCol = c;
                    ImGui::PopID();
                }
                for (size_t r = 0; r < t.rows.size(); ++r) {
                    IntroEdit::TableEdit::Row& row = t.rows[r];
                    row.cells.resize(static_cast<size_t>(cols));
                    ImGui::PushID(static_cast<int>(r) + 100000);
                    ImGui::TableNextRow();
                    col = 0;
                    if (dice) {
                        ImGui::TableSetColumnIndex(col++);
                        inputStr("##roll", row.roll, -FLT_MIN, "1-2");
                    }
                    for (int c = 0; c < cols; ++c) {
                        ImGui::TableSetColumnIndex(col++);
                        inputStr(("##" + std::to_string(c)).c_str(), row.cells[static_cast<size_t>(c)]);
                    }
                    ImGui::TableSetColumnIndex(col);
                    if (ImGui::SmallButton("x")) removeRow = static_cast<int>(r);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removeCol >= 0) {
                t.columns.erase(t.columns.begin() + removeCol);
                for (IntroEdit::TableEdit::Row& row : t.rows)
                    if (static_cast<size_t>(removeCol) < row.cells.size()) row.cells.erase(row.cells.begin() + removeCol);
            }
            if (removeRow >= 0) t.rows.erase(t.rows.begin() + removeRow);
            if (ImGui::SmallButton("Add row")) {
                IntroEdit::TableEdit::Row row;
                row.cells.resize(t.columns.size());
                if (dice) row.roll = std::to_string(t.rows.size() + 1);
                t.rows.push_back(std::move(row));
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Add column")) {
                t.columns.push_back("Column " + std::to_string(t.columns.size() + 1));
                for (IntroEdit::TableEdit::Row& row : t.rows) row.cells.resize(t.columns.size());
            }
            ImGui::SameLine();
            const std::string marker = "{{table: " + t.name + "}}";
            if (ImGui::SmallButton("Put in a section...")) ImGui::OpenPopup("##put");
            if (ImGui::BeginPopup("##put")) {
                ImGui::TextColored(kGrey, "Adds %s to the end of:", marker.c_str());
                for (size_t s = 0; s < edit.sections.size(); ++s)
                    if (ImGui::Selectable((edit.sections[s].title + "##sec" + std::to_string(s)).c_str())) {
                        IntroEdit::Section& sec = edit.sections[s];
                        sec.body += (sec.body.empty() ? "" : "\n") + marker;
                    }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy marker")) ImGui::SetClipboardText(marker.c_str());
        }
        ImGui::PopID();
    }
    if (removeTable >= 0) edit.tables.erase(edit.tables.begin() + removeTable);
    if (ImGui::Button("Add table")) {
        IntroEdit::TableEdit t;
        t.name = "New table";
        t.columns = {"Column 1", "Column 2"};
        t.rows.resize(2);
        for (IntroEdit::TableEdit::Row& row : t.rows) row.cells.resize(2);
        edit.tables.push_back(std::move(t));
        edit.openTable = static_cast<int>(edit.tables.size()) - 1;
    }
    ImGui::SameLine();
    ImGui::TextColored(kGrey, "A table shows where a line \"{{table: Name}}\" is, or at the end of the page.");
}

void drawEditor(Host& host, Kind kind, IntroEdit& edit) {
    if (ImGui::Button("Save")) saveEdit(host, kind, edit);
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        edit.on = false;
        edit.error.clear();
    }
    ImGui::SameLine();
    ImGui::TextColored(kGrey, "Writes %s", introFile(host, kind).c_str());
    if (!edit.error.empty()) ImGui::TextColored(kRed, "%s", edit.error.c_str());

    if (!edit.body.empty()) {                                       // a text with no section of its own (an old intro, a homebrew pack's)
        ImGui::TextColored(kGold, "Text");
        textBox("##ebody", edit.body, 4, 24);
    }

    int remove = -1, up = -1, down = -1;
    for (size_t i = 0; i < edit.sections.size(); ++i) {
        IntroEdit::Section& s = edit.sections[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::TextColored(kGold, "Section %d", static_cast<int>(i) + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && i > 0) up = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("Down") && i + 1 < edit.sections.size()) down = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
        inputStr("##title", s.title, -FLT_MIN, "Section title");
        textBox("##body", s.body, 3, 24);
        ImGui::PopID();
    }
    if (up > 0) std::swap(edit.sections[static_cast<size_t>(up)], edit.sections[static_cast<size_t>(up) - 1]);
    if (down >= 0) std::swap(edit.sections[static_cast<size_t>(down)], edit.sections[static_cast<size_t>(down) + 1]);
    if (remove >= 0) edit.sections.erase(edit.sections.begin() + remove);
    ImGui::Separator();
    if (ImGui::Button("Add section")) edit.sections.push_back({"New section", ""});

    drawTables(edit);
}

}  // namespace

void openIntroSection(Host& host, Kind kind, int section, int line) {
    g_pending = {kind, section, line};
    host.showModule(kindPage(kind));
}

void introPage(Host& host, Kind kind, IntroEdit& edit, const char* id) {
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);           // the arrows scroll the text (keyScroll), not ImGui's nav
    ImGui::BeginChild(id, ImVec2(0, 0));
    keyScroll();
    const Intro& intro = host.content().introOf(kind);
    if (edit.on) {
        drawEditor(host, kind, edit);
    } else {
        if (ImGui::SmallButton("Edit")) beginEdit(host, kind, edit);
        if (intro.empty()) ImGui::TextColored(kGrey, "Nothing here yet. Edit adds text to this page.");
        std::vector<bool> used(intro.tables.size(), false);
        if (!intro.body.empty()) drawBody(intro, intro.body, "introbody", used);
        for (size_t i = 0; i < intro.sections.size(); ++i) {
            const RuleNode::Section& sec = intro.sections[i];
            ImGui::Spacing();
            if (g_pending.kind == kind && g_pending.section == static_cast<int>(i)) {
                if (g_pending.line > 0) scrollToLine("introsec" + std::to_string(i), g_pending.line);   // the line itself, when it is not the first
                else ImGui::SetScrollHereY(0.0f);                  // reached from a link: this section goes to the top
                g_pending.section = -1;
            }
            bigText(sec.title.c_str(), 1.1f, kGold);
            drawBody(intro, sec.body, "introsec" + std::to_string(i), used);
        }
        for (size_t i = 0; i < intro.tables.size(); ++i) {
            if (used[i]) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Spacing();
            bigText(intro.tables[i].title.c_str(), 1.1f, kAccent);
            tableGrid(intro.tables[i], -1);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopItemFlag();
}

void introTabs(Host& host, Kind kind, IntroTabs& tabs, const char* id, const char* pageLabel, const std::function<void()>& page) {
    if (g_pending.section >= 0 && g_pending.kind == kind) {
        if (g_pending.section >= static_cast<int>(host.content().introOf(kind).sections.size())) g_pending.section = -1;   // no longer there
    }
    if (g_pending.section >= 0 && g_pending.kind == kind) {
        tabs.wantIntro = true;
        tabs.edit.on = false;                                      // (a section can only be shown, not edited)
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
        introPage(host, kind, tabs.edit, "##intro");
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

}  // namespace gm::ui
