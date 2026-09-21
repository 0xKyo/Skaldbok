#include "modules/character_sheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

#include <imgui.h>

#include "creation.h"
#include "encounter.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

// What is drawn by hand follows the printed sheet's own colours.
constexpr ImU32 kGreen = IM_COL32(47, 122, 104, 255), kGreenDark = IM_COL32(30, 90, 76, 255), kGreenBand = IM_COL32(37, 105, 89, 255);
constexpr ImU32 kGreenLight = IM_COL32(169, 201, 189, 255), kBannerText = IM_COL32(243, 234, 210, 255), kYellow = IM_COL32(230, 198, 106, 255);
constexpr ImU32 kParchment = IM_COL32(233, 222, 185, 255), kParchmentEdge = IM_COL32(183, 162, 111, 255), kPaperField = IM_COL32(250, 243, 222, 255);
constexpr ImU32 kRule = IM_COL32(185, 168, 132, 255), kSheetRed = IM_COL32(226, 69, 62, 255), kSheetRedDark = IM_COL32(176, 35, 27, 255);

ImU32 inkColor() { return ImGui::GetColorU32(ImGuiCol_Text); }

void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 edge) {
    const ImVec2 pts[4] = {{c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}};
    if (fill) dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, edge, 1.2f, ImDrawFlags_Closed);
}

void centeredText(ImDrawList* dl, ImVec2 center, float size, ImU32 color, const char* text) {
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(ImGui::GetFont(), size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), color, text);
}

// The green ornamented bar that heads every block of the printed sheet.
void banner(const char* text, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFontSize() + U(10), r = U(6);
    const ImVec2 q(p.x + width, p.y + h);
    dl->AddRectFilled(p, q, kGreen, r);
    dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(q.x - 2, p.y + h * 0.5f), IM_COL32(255, 255, 255, 28), r, ImDrawFlags_RoundCornersTop);
    dl->AddRect(p, q, kGreenDark, r, U(1.5f));
    diamond(dl, ImVec2(p.x + U(13), p.y + h * 0.5f), U(4.5f), kYellow, kGreenDark);
    diamond(dl, ImVec2(q.x - U(13), p.y + h * 0.5f), U(4.5f), kYellow, kGreenDark);
    centeredText(dl, ImVec2(p.x + width * 0.5f, p.y + h * 0.5f), ImGui::GetFontSize(), kBannerText, text);
    ImGui::Dummy(ImVec2(width, h));
}

// A banner with a parchment box beside it (DAMAGE BON. STR, GOLD...); `value(boxWidth)` draws what goes in the box.
template <class F>
void bannerBox(const char* label, float width, F value) {
    const float boxW = std::max(U(72), width * 0.34f), gap = U(6), h = ImGui::GetFontSize() + U(10);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    banner(label, width - boxW - gap);
    ImGui::SameLine(0, gap);
    const ImVec2 b = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(b, ImVec2(b.x + boxW, b.y + h), kParchment, U(5));
    dl->AddRect(b, ImVec2(b.x + boxW, b.y + h), kParchmentEdge, U(5));
    ImGui::SetCursorScreenPos(ImVec2(b.x + U(6), b.y + (h - ImGui::GetFrameHeight()) * 0.5f));
    value(boxW - U(12));
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(width, U(6)));
}

void centeredLabel(float width, const char* text, ImVec4 color) {
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (width - ImGui::CalcTextSize(text).x) * 0.5f));
    ImGui::TextColored(color, "%s", text);
}

// Text and number fields drawn as a line on the paper, like the blanks of the printed sheet.
struct BlankStyle {
    BlankStyle() {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 3));
    }
    ~BlankStyle() {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }
};

void underline() {
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y - 1), ImVec2(mx.x, mx.y - 1), kRule, 1.0f);
}

bool lineText(const char* id, std::string& s, float width, const char* hint = nullptr) {
    BlankStyle style;
    const bool changed = inputStr(id, s, width, hint);
    underline();
    return changed;
}

bool lineInt(const char* id, int& v, int lo, int hi, float width) {
    BlankStyle style;
    const bool changed = smallInt(id, v, lo, hi, width / U(1.0f));
    underline();
    return changed;
}

const char* const kBaneArmor = "Sneaking · Evade · Acrobatics";
const char* const kBaneHelmet = "Awareness · Ranged attacks";

std::string statText(const Entry* e, const char* label) {
    if (e)
        for (const Field& f : e->fields)
            if (f.label == label) return f.value;
    return {};
}

const Entry* linkedGear(const ContentStore& cs, const Item& it) {
    if (it.key.empty()) return nullptr;
    for (Kind k : {Kind::Weapon, Kind::Armor, Kind::Gear})
        if (const Entry* e = cs.entry(k, cs.idByKey(k, it.key))) return e;
    return nullptr;
}

}  // namespace

void CharacterSheet::reset() {
    editingAttribute_ = -1;
    message_.clear();
    newItem_.clear();
}

// ----------------------------------------------------------------------------------------------- the page

bool CharacterSheet::draw(Character& c) {
    bool changed = false;
    const float w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    const float gap = U(14);

    const std::vector<sheet::OpenIssue> issues = sheet::openIssues(c);
    flagged_.clear();
    for (const sheet::OpenIssue& i : issues) flagged_.insert(i.key);
    oversight(c, changed, w, issues);
    header(c, changed, w);
    attributes(c, changed, w);

    // the three blocks under the gems: damage bonuses and movement
    {
        const ContentStore& content = host_.content();
        int kinMove = 0;
        if (const Entry* k = entryFor(content, Kind::Kin, c.kin)) kinMove = std::atoi(k->prop("movement").c_str());
        const float cell = (w - 2 * gap) / 3;
        const std::string strBonus = damageBonus(c.attr[0]), aglBonus = damageBonus(c.attr[2]);
        const std::string move = kinMove > 0 ? std::to_string(kinMove + movementModifier(c.attr[2])) : "?";
        const std::pair<const char*, std::string> cells[3] = {{"DAMAGE BON. STR", strBonus.empty() ? "–" : strBonus}, {"DAMAGE BON. AGL", aglBonus.empty() ? "–" : aglBonus}, {"MOVEMENT", move}};
        ImGui::BeginGroup();
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(0, gap);
            ImGui::BeginGroup();
            bannerBox(cells[i].first, cell, [&](float bw) { centeredLabel(bw, cells[i].second.c_str(), ImGui::GetStyle().Colors[ImGuiCol_Text]); });
            ImGui::EndGroup();
        }
        ImGui::EndGroup();
    }

    // three columns like the printed page: abilities and coins | skills | inventory
    const float leftW = (w - 2 * gap) * 0.21f, rightW = (w - 2 * gap) * 0.29f, midW = w - 2 * gap - leftW - rightW;
    const ImGuiChildFlags cf = ImGuiChildFlags_AutoResizeY;
    const ImGuiWindowFlags wf = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##colL", ImVec2(leftW, 0), cf, wf);
    abilities(c, changed, leftW);
    coins(c, changed, leftW);
    ImGui::EndChild();
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##colM", ImVec2(midW, 0), cf, wf);
    skills(c, changed, midW);
    ImGui::EndChild();
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##colR", ImVec2(rightW, 0), cf, wf);
    inventory(c, changed, rightW);
    ImGui::EndChild();

    // bottom: armor, helmet and weapons | willpower and hit points
    const float bottomLeft = w - rightW - gap;
    ImGui::BeginChild("##rowL", ImVec2(bottomLeft, 0), cf, wf);
    armorAndWeapons(c, changed, bottomLeft);
    ImGui::EndChild();
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##rowR", ImVec2(rightW, 0), cf, wf);
    points("WILLPOWER POINTS", c.wp, maxWp(c), c.wpBonus, "Added to WIL for the maximum (the heroic ability Focused, for example)", false, changed, rightW);
    points("HIT POINTS", c.hp, maxHp(c), c.hpBonus, "Added to CON for the maximum (the heroic ability Robust, for example)", true, changed, rightW);
    ImGui::EndChild();
    c.hp = std::min(c.hp, maxHp(c));
    c.wp = std::min(c.wp, maxWp(c));

    ImGui::Spacing();
    ImGui::TextColored(kGold, "Notes");
    changed |= inputMultiline("##notes", c.notes, ImVec2(-FLT_MIN, U(90)));
    if (changed) sheet::prune(c);                                   // a ruling lasts only while what it judged is unchanged
    return changed;
}

// ------------------------------------------------------------------------- what the players did on the web

// The GM's view of a sheet the player can also edit: whether they may, what they did that the rules do not allow (to approve or
// reject), and what they changed lately (to undo).
void CharacterSheet::oversight(Character& c, bool& changed, float w, const std::vector<sheet::OpenIssue>& issues) {
    bool locked = c.locked;
    if (ImGui::Checkbox("Lock this sheet: the player cannot change it from the web", &locked)) {
        c.locked = locked;
        changed = true;
    }

    for (const sheet::OpenIssue& issue : issues) {
        ImGui::PushID(issue.key.c_str());
        Backdrop box;
        box.begin(w, IM_COL32(250, 224, 216, 255), kSheetRed, U(8), 8.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kRed, issue.status == "rejected" ? "REJECTED" : "RULE CHECK");
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - U(250));
        ImGui::TextUnformatted(issue.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SameLine(ImGui::GetCursorPosX() + std::max(0.0f, w - U(240) - ImGui::GetCursorPosX()));
        if (ImGui::Button("Validate")) {
            changed |= sheet::review(c, issue.key, true);
            host_.notify("Validated: " + issue.message);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("It is normal now: the flag goes away.");
        ImGui::SameLine();
        ImGui::BeginDisabled(issue.status == "rejected");
        if (ImGui::Button("Reject")) changed |= sheet::review(c, issue.key, false);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("It stays flagged: the player has to change it by hand.");
        box.end();
        ImGui::PopID();
    }

    const std::vector<ChangeEntry> entries = host_.changes().entries(c.id);
    if (entries.empty()) return;
    const std::string title = "Changes made by the player (" + std::to_string(entries.size()) + ")";
    if (!ImGui::CollapsingHeader(title.c_str())) return;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const ChangeEntry& e = *it;
        ImGui::PushID(e.id.c_str());
        ImGui::TextColored(kGrey, "%s %s", e.at.size() >= 16 ? e.at.substr(5, 5).c_str() : "", e.at.size() >= 16 ? e.at.substr(11, 5).c_str() : "");
        ImGui::SameLine();
        ImGui::TextColored(e.undone ? kGrey : kGold, "%s", e.by == "gm" ? "GM" : "player");
        ImGui::SameLine();
        ImGui::BeginDisabled(e.undone);
        if (ImGui::SmallButton("Undo")) {
            json doc;
            jsonParse(c.toJson(), doc, nullptr);
            sheet::merge(doc, e.before);
            Character undone;
            if (Character::fromJson(doc.dump(), undone, nullptr)) {
                undone.id = c.id;
                c = undone;
                changed = true;
                host_.changes().markUndone(c.id, e.id);
                host_.notify("Undone: " + e.summary);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - U(190));
        ImGui::TextColored(e.undone ? kGrey : ImGui::GetStyle().Colors[ImGuiCol_Text], "%s%s", e.summary.c_str(), e.undone ? "  (undone)" : "");
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
    ImGui::Spacing();
}

// ----------------------------------------------------------------------------------------------- header

void CharacterSheet::header(Character& c, bool& changed, float w) {
    const float gap = U(14), side = (w - 2 * gap) * 0.28f, mid = w - 2 * gap - 2 * side;
    ImGui::BeginGroup();

    // identity, like the lines at the top left of the sheet
    ImGui::BeginGroup();
    const float labelX = U(84);
    auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kGrey, "%s", label);
        ImGui::SameLine(labelX);
    };
    row("PLAYER");
    changed |= lineText("##player", c.player, side - labelX);
    row("KIN");
    ImGui::TextUnformatted(c.kin.name.c_str());
    row("AGE");
    ImGui::SetNextItemWidth(std::min(U(110), side - labelX));
    if (ImGui::BeginCombo("##age", ageRule(c.age).label)) {
        for (const char* a : {"young", "adult", "old"})
            if (ImGui::Selectable(ageRule(a).label, c.age == a)) {
                c.age = a;
                changed = true;
            }
        ImGui::EndCombo();
    }
    row("PROFESSION");
    ImGui::TextUnformatted(c.profession.name.c_str());
    if (!c.school.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "· %s", c.school.c_str());
    }
    row("WEAKNESS");
    changed |= lineText("##weak", c.weakness, side - labelX);
    if (const Party* party = host_.parties().partyOf(c.id)) {
        row("PARTY");
        ImGui::TextColored(kAccent, "%s", party->name.c_str());
    }
    ImGui::EndGroup();

    // the scroll with the name
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = U(96);
        dl->AddRectFilled(p, ImVec2(p.x + mid, p.y + h), IM_COL32(232, 218, 176, 255), U(8));
        dl->AddRectFilled(p, ImVec2(p.x + mid, p.y + h * 0.5f), IM_COL32(255, 255, 255, 40), U(8), ImDrawFlags_RoundCornersTop);
        dl->AddRect(p, ImVec2(p.x + mid, p.y + h), kParchmentEdge, U(8), U(1.5f));
        dl->AddText(ImVec2(p.x + U(10), p.y + U(4)), ImGui::GetColorU32(kGrey), "NAME");
        ImGui::SetCursorScreenPos(ImVec2(p.x + U(14), p.y + U(24)));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
        changed |= lineText("##name", c.name, mid - U(28), "Name");
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(p.x + U(14), p.y + h - ImGui::GetFrameHeight() - U(6)));
        changed |= lineText("##nick", c.nickname, mid - U(28), "Nickname");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        ImGui::Dummy(ImVec2(mid, U(4)));
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kGrey, "APPEARANCE");
    {
        Backdrop box;
        box.begin(side, kParchment, kParchmentEdge, U(4));
        BlankStyle style;
        changed |= inputMultiline("##appearance", c.appearance, ImVec2(side - U(8), U(58)));
        box.end();
    }
    ImGui::EndGroup();
    ImGui::EndGroup();
    ImGui::Spacing();
}

// ------------------------------------------------------------------------------------ attributes and conditions

void CharacterSheet::attributes(Character& c, bool& changed, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = U(128), step = w / kAttrCount, radius = U(26), fs = ImGui::GetFontSize();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kGreenBand, U(16));
    dl->AddRectFilled(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + w - 3, p.y + h * 0.4f), IM_COL32(255, 255, 255, 20), U(14), ImDrawFlags_RoundCornersTop);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), kGreenDark, U(16), U(2.5f));

    for (int i = 0; i < kAttrCount; ++i) {
        const float cx = p.x + step * (static_cast<float>(i) + 0.5f);
        const ImVec2 center(cx, p.y + U(54));
        ImGui::PushID(i);
        centeredText(dl, ImVec2(cx, p.y + U(14)), fs, kBannerText, kAttrShort[i]);

        ImGui::SetCursorScreenPos(ImVec2(cx - radius, center.y - radius));
        ImGui::InvisibleButton("##gem", ImVec2(radius * 2, radius * 2));
        if (ImGui::IsItemClicked()) {
            editingAttribute_ = i;
            focusAttribute_ = true;
        }
        if (ImGui::IsItemHovered() && editingAttribute_ != i) {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);                       // the wheel changes the number, it does not scroll the page
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0) {
                c.attr[i] = std::clamp(c.attr[i] + (wheel > 0 ? 1 : -1), 1, 30);
                changed = true;
            }
            ImGui::SetTooltip("%s: click to type a value, or use the mouse wheel", kAttrLong[i]);
        }
        dl->AddCircleFilled(center, radius, kPaperField);
        dl->AddCircle(center, radius, flagged_.contains(std::string("attr:") + kAttrShort[i]) ? kSheetRed : kGreenLight, 0, U(4));
        dl->AddCircle(center, radius - U(2), kGreenDark, 0, 1.5f);
        if (editingAttribute_ == i) {
            ImGui::SetCursorScreenPos(ImVec2(center.x - U(22), center.y - ImGui::GetFrameHeight() * 0.5f));
            ImGui::SetNextItemWidth(U(44));
            if (focusAttribute_) {
                ImGui::SetKeyboardFocusHere();
                focusAttribute_ = false;
            }
            int v = c.attr[i];
            if (ImGui::InputInt("##edit", &v, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                c.attr[i] = std::clamp(v, 1, 30);
                changed = true;
                editingAttribute_ = -1;
            } else if (ImGui::IsItemDeactivated()) {
                editingAttribute_ = -1;
            }
        } else {
            char num[8];
            std::snprintf(num, sizeof num, "%d", c.attr[i]);
            centeredText(dl, center, fs * 1.7f, inkColor(), num);
        }
        char base[24];
        std::snprintf(base, sizeof base, "base chance %d", baseChance(c.attr[i]));
        centeredText(dl, ImVec2(cx, p.y + U(92)), fs * 0.85f, kGreenLight, base);

        // the condition of this attribute: a diamond that lights up red
        for (int b = 0; b < 6; ++b) {
            if (std::string(kConditions[b].attribute) != kAttrShort[i]) continue;
            const bool on = c.conditions & (1u << b);
            const ImVec2 spot(cx - step * 0.5f + U(3), p.y + U(104));
            ImGui::SetCursorScreenPos(spot);
            ImGui::InvisibleButton("##cond", ImVec2(step - U(6), U(20)));
            if (ImGui::IsItemClicked()) {
                c.conditions ^= 1u << b;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: a bane on %s and the skills based on it", kConditions[b].name, kConditions[b].attribute);
            const ImVec2 lc(spot.x + (step - U(6)) * 0.5f, spot.y + U(10));
            const std::string label = kConditions[b].name;
            const float lw = ImGui::CalcTextSize(label.c_str()).x * 0.85f;
            const ImVec2 dc(lc.x - lw * 0.5f - U(9), lc.y);
            dl->AddRectFilled(ImVec2(dc.x - U(8), spot.y + U(1)), ImVec2(lc.x + lw * 0.5f + U(7), spot.y + U(19)), on ? kSheetRed : IM_COL32(23, 73, 62, 255), U(9));
            diamond(dl, dc, U(4), on ? IM_COL32(255, 255, 255, 255) : 0, on ? IM_COL32(255, 255, 255, 255) : kGreenLight);
            centeredText(dl, lc, fs * 0.85f, on ? IM_COL32(255, 255, 255, 255) : kBannerText, label.c_str());
            break;
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, U(10)));
}

// ----------------------------------------------------------------------------------- abilities, spells, coins

bool CharacterSheet::refList(const char* label, std::vector<Ref>& list, Kind kind, const char* popup, float w) {
    bool changed = false;
    const ContentStore& content = host_.content();
    ImGui::TextColored(kGold, "%s", label);
    for (size_t i = 0; i < list.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("x")) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        const Entry* e = entryFor(content, kind, list[i]);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - U(30));
        ImGui::TextWrapped("%s", list[i].name.c_str());
        ImGui::PopTextWrapPos();
        entryTooltip(e);
        ImGui::PopID();
    }
    if (ImGui::SmallButton((std::string("Add ") + (kind == Kind::Spell ? "spell" : "ability") + "…").c_str())) ImGui::OpenPopup(popup);
    const Entry* picked = nullptr;
    if (pickEntry(content, popup, {kind}, picked) && picked) {
        list.push_back({picked->key, picked->title});
        changed = true;
    }
    return changed;
}

void CharacterSheet::abilities(Character& c, bool& changed, float w) {
    banner("ABILITIES & SPELLS", w);
    ImGui::Spacing();
    changed |= refList("Abilities", c.abilities, Kind::Ability, "##addability", w);
    ImGui::Spacing();
    changed |= refList("Spells", c.spells, Kind::Spell, "##addspell", w);
    ImGui::Spacing();
}

void CharacterSheet::coins(Character& c, bool& changed, float w) {
    ImGui::Spacing();
    struct Coin {
        const char* label;
        int* value;
    };
    for (const Coin& coin : {Coin{"GOLD", &c.gold}, Coin{"SILVER", &c.silver}, Coin{"COPPER", &c.copper}}) {
        ImGui::PushID(coin.label);
        bannerBox(coin.label, w, [&](float bw) {
            BlankStyle style;
            changed |= smallInt("##v", *coin.value, 0, 99999, bw / U(1.0f));
        });
        ImGui::PopID();
    }
}

// ------------------------------------------------------------------------------------------------- skills

void CharacterSheet::skills(Character& c, bool& changed, float w) {
    const ContentStore& content = host_.content();
    struct Row {
        std::string name, attr, key;
        int cat;                       // 0 core, 1 weapon, 2 others
        int entry;                     // index in c.skills, -1 if the character has no entry yet
    };
    std::vector<Row> rows;
    auto entryIndex = [&](const std::string& key, const std::string& name) {
        for (size_t i = 0; i < c.skills.size(); ++i)
            if ((!key.empty() && c.skills[i].ref.key == key) || lowered(c.skills[i].ref.name) == lowered(name)) return static_cast<int>(i);
        return -1;
    };
    std::set<int> used;
    for (const Entry* e : selectableSkills(content)) {
        const int idx = entryIndex(e->key, e->title);
        if (idx >= 0) used.insert(idx);
        rows.push_back({e->title, e->prop("attribute"), e->key, e->prop("category") == "core" ? 0 : e->prop("category") == "weapon" ? 1 : 2, idx});
    }
    for (size_t i = 0; i < c.skills.size(); ++i)          // skills the character has that are not on the list (schools of magic, removed packs)
        if (!used.contains(static_cast<int>(i))) rows.push_back({c.skills[i].ref.name, c.skills[i].attribute, c.skills[i].ref.key, 2, static_cast<int>(i)});
    std::ranges::stable_sort(rows, [](const Row& a, const Row& b) { return a.cat != b.cat ? a.cat < b.cat : lowered(a.name) < lowered(b.name); });

    Backdrop panel;
    panel.begin(w, kParchment, kParchmentEdge, U(10));
    const float inner = w - U(20);
    banner("SKILLS", inner);

    int marked = 0;
    for (const SkillEntry& s : c.skills) marked += s.marked;
    if (ImGui::SmallButton(("Advancement rolls (" + std::to_string(marked) + " marked)").c_str()) && marked > 0) {
        std::string result;
        for (SkillEntry& s : c.skills) {
            if (!s.marked) continue;
            const int level = skillLevel(c, &s, s.attribute), r = host_.dice().roll(20);
            const bool up = r > level && level < 18;
            if (up) s.level = level + 1;
            s.marked = false;
            result += (result.empty() ? "" : "; ") + s.ref.name + " " + std::to_string(r) + (up ? " → " + std::to_string(level + 1) : " no change");
        }
        host_.notify("Advancement: " + result);
        message_ = "Advancement (D20 above the level raises it): " + result;
        host_.flashRoll();
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("At the end of a session: roll D20 for each mark; above the skill level raises it by one (max 18)");
    if (!message_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
        ImGui::TextColored(kGold, "%s", message_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::TextColored(kGrey, "Diamond: advancement mark. Name: train it. (ATTR): roll a D20.");

    auto rowFor = [&](const Row& r) {
        SkillEntry blank;
        blank.ref = {r.key, r.name};
        blank.attribute = r.attr;
        auto current = [&]() -> const SkillEntry& { return r.entry >= 0 ? c.skills[static_cast<size_t>(r.entry)] : blank; };
        auto ensure = [&]() -> SkillEntry& {                       // the entry is created the first time something is set
            if (r.entry >= 0) return c.skills[static_cast<size_t>(r.entry)];
            c.skills.push_back(blank);
            return c.skills.back();
        };
        const int attr = std::max(0, attrIndex(r.attr));
        ImGui::PushID(r.name.c_str());
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec2 dp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##mark", ImVec2(U(18), ImGui::GetFrameHeight()));
        if (ImGui::IsItemClicked()) {
            ensure().marked = !current().marked;
            changed = true;
        }
        diamond(dl, ImVec2(dp.x + U(9), dp.y + ImGui::GetFrameHeight() * 0.5f), U(5.5f), current().marked ? kSheetRed : kPaperField, current().marked ? kSheetRedDark : IM_COL32(122, 75, 42, 255));
        ImGui::SameLine(0, U(2));

        int level = skillLevel(c, r.entry >= 0 ? &current() : nullptr, r.attr);
        const bool broken = flagged_.contains("skill:" + r.name);
        if (broken) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.745f, 0.18f, 0.149f, 1));
        const bool levelChanged = lineInt("##level", level, 1, 20, 38);
        if (broken) ImGui::PopStyleColor();
        if (levelChanged) {
            SkillEntry& s = ensure();
            s.level = level == baseChance(c.attr[attr]) && !s.trained ? 0 : level;
            changed = true;
        }
        ImGui::SameLine(0, U(6));
        ImGui::AlignTextToFramePadding();
        const bool trained = current().trained;
        ImGui::TextColored(trained ? ImGui::GetStyle().Colors[ImGuiCol_Text] : kGrey, "%s", r.name.c_str());
        if (ImGui::IsItemClicked()) {
            SkillEntry& s = ensure();
            s.trained = !s.trained;
            s.level = s.trained ? std::min(18, 2 * baseChance(c.attr[attr])) : 0;
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(trained ? "Trained. Click to make it untrained." : "Untrained (base chance). Click to train it.");
        ImGui::SameLine(0, U(4));
        ImGui::TextColored(ImGui::IsMouseHoveringRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + ImGui::CalcTextSize("(AGL)").x, ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight())) ? kAccent : kGrey,
                           "(%s)", r.attr.c_str());
        if (ImGui::IsItemClicked()) {
            const int d = host_.dice().roll(20);
            message_ = r.name + ": rolled " + std::to_string(d) + " against " + std::to_string(level) + " — " + Dice::checkOutcome(d, level);
            host_.flashRoll();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to roll a D20 against %d", level);
        ImGui::PopID();
    };

    // core skills on the left, weapon skills and the rest on the right, as printed
    const float colGap = U(16), colW = (inner - colGap) * 0.5f, x0 = ImGui::GetCursorPosX();
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, U(1)));
    ImGui::BeginGroup();
    ImGui::TextColored(kGold, "SKILLS");
    for (const Row& r : rows)
        if (r.cat == 0 && (attrIndex(r.attr) >= 0 || r.entry >= 0)) rowFor(r);
    ImGui::EndGroup();
    ImGui::SameLine(x0 + colW + colGap);
    ImGui::BeginGroup();
    for (int cat : {1, 2}) {
        const bool any = std::ranges::any_of(rows, [&](const Row& r) { return r.cat == cat; });
        if (!any) continue;
        ImGui::TextColored(kGold, cat == 1 ? "WEAPON SKILLS" : "SECONDARY SKILLS");
        for (const Row& r : rows)
            if (r.cat == cat && (attrIndex(r.attr) >= 0 || r.entry >= 0)) rowFor(r);
        ImGui::Spacing();
    }
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    panel.end();

    // entries that carry no information any more are dropped
    changed |= std::erase_if(c.skills, [](const SkillEntry& s) { return !s.trained && !s.marked && s.level == 0; }) > 0;
}

// -------------------------------------------------------------------------------- inventory, memento, tiny items

void CharacterSheet::inventory(Character& c, bool& changed, float w) {
    const int limit = encumbranceLimit(c), carriedNow = carriedItems(c);
    bannerBox("INVENTORY", w, [&](float bw) {
        char text[24];
        std::snprintf(text, sizeof text, "%d / %d", carriedNow, limit);
        ImGui::PushStyleColor(ImGuiCol_Text, carriedNow > limit ? ImVec4(0.745f, 0.18f, 0.149f, 1) : ImGui::GetStyle().Colors[ImGuiCol_Text]);
        centeredLabel(bw, text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
        ImGui::PopStyleColor();
    });
    ImGui::TextColored(kGrey, "Encumbrance limit %d", limit);

    const float numberW = U(20);
    const size_t lines = std::max<size_t>({8u, c.inventory.size() + 1, static_cast<size_t>(limit)});
    for (size_t i = 0; i < lines; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kGrey, "%zu", i + 1);
        ImGui::SameLine(numberW);
        if (i < c.inventory.size()) {
            Item& it = c.inventory[i];
            changed |= lineText("##n", it.name, w - numberW - U(76));
            ImGui::SameLine(0, U(4));
            changed |= lineInt("##c", it.count, 1, 999, 36);
            ImGui::SameLine(0, U(4));
            if (ImGui::SmallButton("x")) {
                c.inventory.erase(c.inventory.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
        } else if (i == c.inventory.size()) {                       // the free line: typing in it adds an item
            if (lineText("##new", newItem_, w - numberW - U(76), "type to add an item")) {
                c.inventory.push_back(Item{newItem_, "", 1, ""});
                newItem_.clear();
                changed = true;
            }
        } else {
            ImGui::Dummy(ImVec2(w - numberW, ImGui::GetFrameHeight()));
            underline();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextColored(kGold, "MEMENTO");
    changed |= lineText("##memento", c.memento, w);
    ImGui::Spacing();

    Backdrop tiny;
    tiny.begin(w, kParchment, kParchmentEdge, U(8));
    ImGui::TextColored(kGold, "TINY ITEMS");
    ImGui::SameLine();
    ImGui::TextColored(kGrey, "(fit in a closed fist)");
    for (size_t i = 0; i < c.tinyItems.size(); ++i) {
        ImGui::PushID(static_cast<int>(i) + 1000);
        changed |= lineText("##t", c.tinyItems[i], w - U(52));
        ImGui::SameLine(0, U(4));
        if (ImGui::SmallButton("x")) {
            c.tinyItems.erase(c.tinyItems.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Add tiny item")) {
        c.tinyItems.emplace_back();
        changed = true;
    }
    tiny.end();
}

// ------------------------------------------------------------------------------- armor, helmet and weapons

void CharacterSheet::gear(const char* label, Item& item, const char* bane, bool& changed, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Entry* e = linkedGear(host_.content(), item);
    std::string rating = statText(e, "Armor rating");
    if (rating.empty()) rating = "–";
    const float r = U(20);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + r + U(2)), r, IM_COL32(236, 232, 218, 255));
    dl->AddCircle(ImVec2(p.x + r, p.y + r + U(2)), r, IM_COL32(139, 143, 138, 255), 0, U(3));
    centeredText(dl, ImVec2(p.x + r, p.y + r + U(2)), ImGui::GetFontSize() * 1.2f, inkColor(), rating.c_str());
    ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + U(10), p.y));
    ImGui::BeginGroup();
    ImGui::TextColored(kGold, "%s", label);
    changed |= lineText("##name", item.name, w - r * 2 - U(10));
    ImGui::TextColored(kGrey, "Bane on: %s", bane);
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(ImGui::GetItemRectMax().y, p.y + r * 2 + U(6))));
    ImGui::Dummy(ImVec2(w, U(4)));
}

void CharacterSheet::armorAndWeapons(Character& c, bool& changed, float w) {
    const ContentStore& content = host_.content();
    const float gap = U(14), half = (w - gap) * 0.5f;
    ImGui::BeginGroup();
    ImGui::PushID("armor");
    gear("ARMOR", c.armor, kBaneArmor, changed, half);
    ImGui::PopID();
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    ImGui::PushID("helmet");
    gear("HELMET", c.helmet, kBaneHelmet, changed, half);
    ImGui::PopID();
    ImGui::EndGroup();

    banner("WEAPON / SHIELD", w);
    const float nameW = w * 0.30f, cellW = w * 0.09f, featureX = nameW + 4 * cellW + U(10), x0 = ImGui::GetCursorPosX();
    const char* const heads[] = {"GRIP", "RANGE", "DAMAGE", "DURAB."};
    ImGui::SetCursorPosX(x0 + nameW);
    ImGui::TextColored(kGrey, "%s", heads[0]);
    for (int k = 1; k < 4; ++k) {
        ImGui::SameLine(x0 + nameW + static_cast<float>(k) * cellW);
        ImGui::TextColored(kGrey, "%s", heads[k]);
    }
    ImGui::SameLine(x0 + featureX);
    ImGui::TextColored(kGrey, "FEATURES");
    for (size_t i = 0; i < c.weapons.size(); ++i) {
        ImGui::PushID(static_cast<int>(i) + 500);
        Item& it = c.weapons[i];
        const Entry* e = linkedGear(content, it);
        changed |= lineText("##n", it.name, nameW - U(8));
        int k = 0;
        for (const char* label : {"Grip", "Range", "Damage", "Durability"}) {
            const std::string v = statText(e, label);
            ImGui::SameLine(x0 + nameW + static_cast<float>(k++) * cellW);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(v.empty() ? "–" : v.c_str());
        }
        ImGui::SameLine(x0 + featureX);
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(x0 + w - U(30));
        ImGui::TextWrapped("%s", e && !e->body.empty() ? e->body.substr(0, e->body.find('\n')).c_str() : "");
        ImGui::PopTextWrapPos();
        ImGui::SameLine(x0 + w - U(24));
        if (ImGui::SmallButton("x")) {
            c.weapons.erase(c.weapons.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Add weapon…")) ImGui::OpenPopup("##addweapon");
    ImGui::SameLine();
    if (ImGui::SmallButton("Add gear, armor…")) ImGui::OpenPopup("##addgear");
    const Entry* picked = nullptr;
    if (pickEntry(content, "##addweapon", {Kind::Weapon}, picked) && picked) {
        c.weapons.push_back(Item{picked->title, picked->key, 1, ""});
        changed = true;
    }
    if (pickEntry(content, "##addgear", {Kind::Armor, Kind::Weapon, Kind::Gear}, picked) && picked) {
        Item it;
        it.name = picked->title;
        it.key = picked->key;
        if (picked->kind == Kind::Armor && picked->prop("slot") == "helmet") c.helmet = it;
        else if (picked->kind == Kind::Armor) c.armor = it;
        else if (picked->kind == Kind::Weapon) c.weapons.push_back(it);
        else c.inventory.push_back(it);
        changed = true;
    }
}

// ------------------------------------------------------------------------------ willpower and hit points

void CharacterSheet::points(const char* title, int& current, int max, int& bonus, const char* bonusTip, bool red, bool& changed, float w) {
    Backdrop panel;
    panel.begin(w, red ? kSheetRed : kGreen, red ? kSheetRedDark : kGreenDark, U(10));
    const bool flagged = flagged_.contains(red ? "hp" : "wp") || flagged_.contains(red ? "hpmax" : "wpmax");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kBannerText));
    ImGui::TextUnformatted(flagged ? (std::string(title) + "   ! CHECK THE RULES").c_str() : title);
    ImGui::PopStyleColor();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::BeginGroup();
    // the number now, with quick buttons
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bw = U(62), bh = U(46);
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), kPaperField, U(6));
        char text[16];
        std::snprintf(text, sizeof text, "%d", current);
        centeredText(dl, ImVec2(p.x + bw * 0.5f, p.y + bh * 0.42f), ImGui::GetFontSize() * 1.6f, inkColor(), text);
        std::snprintf(text, sizeof text, "/ %d", max);
        centeredText(dl, ImVec2(p.x + bw * 0.5f, p.y + bh * 0.82f), ImGui::GetFontSize() * 0.8f, ImGui::GetColorU32(kGrey), text);
        ImGui::Dummy(ImVec2(bw, bh));
        ImGui::PushID(title);
        for (int d : {-1, 1}) {
            if (d > 0) ImGui::SameLine(0, U(2));
            if (ImGui::SmallButton(d < 0 ? "-" : "+")) {
                current = std::clamp(current + d, 0, max);
                changed = true;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();

    // the circles: click one to set the points to it
    ImGui::SameLine(0, U(12));
    ImGui::BeginGroup();
    {
        const float radius = U(8), pitch = radius * 2 + U(5);
        const int perRow = std::clamp(static_cast<int>((w - U(20) - U(62) - U(12)) / pitch), 5, 10), count = std::min(max, 30);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(title);
        for (int i = 0; i < count; ++i) {
            const ImVec2 ctr(p.x + radius + static_cast<float>(i % perRow) * pitch, p.y + radius + static_cast<float>(i / perRow) * pitch);
            ImGui::SetCursorScreenPos(ImVec2(ctr.x - radius, ctr.y - radius));
            ImGui::PushID(i);
            ImGui::InvisibleButton("##pip", ImVec2(radius * 2, radius * 2));
            if (ImGui::IsItemClicked()) {
                current = current == i + 1 ? i : i + 1;
                changed = true;
            }
            ImGui::PopID();
            if (i < current) dl->AddCircleFilled(ctr, radius, IM_COL32(255, 255, 255, 255));
            else dl->AddCircle(ctr, radius, IM_COL32(255, 255, 255, 150), 0, 1.6f);
        }
        ImGui::PopID();
        const int rowsUsed = (count + perRow - 1) / perRow;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + static_cast<float>(std::max(rowsUsed, 1)) * pitch));
        ImGui::Dummy(ImVec2(perRow * pitch, U(2)));
    }
    ImGui::EndGroup();

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kBannerText));
    ImGui::TextUnformatted("bonus");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushID(title);
    changed |= smallInt("##bonus", bonus, -20, 40, 56);
    ImGui::PopID();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bonusTip);
    panel.end();
}

}  // namespace gm
