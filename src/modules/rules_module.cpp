// Rules: the text of the books that is not already a creature, spell, skill, kin or profession, plus the homerules that packs add, and
// the tables, each one inside the section it belongs to. It all comes from packs (rules.json): Core opens first and other
// packs add under it or replace. A table found by a search or a link opens the section that holds it.
//
// One class serves two modules, over the same tree (host_.content().rules()): "Rules" (group Reference) shows everything except the
// adventure, and "Adventures" (its own group) shows only the adventure, with "The Misty Vale" as its top entry instead of the
// otherwise-empty rule that just holds it.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>

#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

// The key of the rule that holds the whole adventure (see content.cpp): the boundary between the two modules' scopes.
const char* const kAdventureKey = "core/rule/adventure-library";

class RulesModule : public Module {
public:
    RulesModule(Host& host, bool adventures) : Module(host), adventures_(adventures) { onContentChanged(); }

    const char* id() const override { return adventures_ ? "adventures" : "rules"; }
    const char* title() const override { return adventures_ ? "Adventures" : "Rules"; }
    const char* summary() const override {
        return adventures_ ? "The adventure that comes with the books, chapter by chapter, with its tables in the section they belong to."
                            : "The books' rules and tables (each table in its section), and the homerules of your packs.";
    }
    const char* group() const override { return adventures_ ? "Adventures" : "Reference"; }
    bool ownsDetail() const override { return true; }
    int badge() const override { return static_cast<int>(at_.size()); }

    bool handles(Kind k, int id) const override {
        if (k != Kind::Table) return false;
        if (id < 0) return true;                                             // "is Kind::Table shown by something": either module will do
        const DataTable* t = host_.content().packTable(id);
        return t && at_.count(t->rule) != 0;
    }

    bool findByName(const std::string& want, Selection& out) const override {
        for (const DataTable& t : host_.content().packTables())
            if (t.rule && at_.count(t.rule) && lowered(t.title) == want) {
                out = {Kind::Table, t.id};
                return true;
            }
        return false;
    }

    void onContentChanged() override {
        nodes_ = host_.content().rules();
        at_.clear();
        children_.assign(nodes_.size(), {});
        roots_.clear();
        rolls_.clear();
        const int adventureId = host_.content().ruleByKey(kAdventureKey);
        // Which side of the boundary a node is on: the adventure rule itself and everything under it is "in the adventure"; the
        // adventure rule itself is never shown (it holds nothing of its own) so its children become Adventures' top-level entries.
        std::vector<bool> inAdventure(nodes_.size(), false);
        std::unordered_map<int, size_t> idToIndex;
        for (size_t i = 0; i < nodes_.size(); ++i) idToIndex[nodes_[i].id] = i;
        for (size_t i = 0; i < nodes_.size(); ++i)
            for (int cur = nodes_[i].id; cur;) {
                if (cur == adventureId) {
                    inAdventure[i] = true;
                    break;
                }
                const auto it = idToIndex.find(cur);
                cur = it == idToIndex.end() ? 0 : nodes_[it->second].parent;
            }
        for (size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].id == adventureId) continue;                      // the container itself is never a node in either tree
            if (inAdventure[i] != adventures_) continue;
            at_[nodes_[i].id] = i;
        }
        for (const auto& [id, i] : at_) {
            const auto p = at_.find(nodes_[i].parent);
            (p == at_.end() ? roots_ : children_[p->second]).push_back(i);
        }
        if (!at_.count(selected_)) selected_ = roots_.empty() ? 0 : nodes_[roots_.front()].id;
    }

    // Reached from a search hit, a creature's related tables or the Master Screen: open the section that holds the table.
    void onSelect(const Selection& s) override {
        if (s.kind != Kind::Table) return;
        const DataTable* t = host_.content().packTable(s.id);
        if (!t || !at_.count(t->rule)) return;
        show(t->rule);
        focusTable_ = t->id;
        focusFrames_ = 12;                       // (the tables above it are only measured as they are drawn: the scroll follows for a few frames)
    }

    void drawList() override {
        inputStr("##rulefilter", filter_, -FLT_MIN, adventures_ ? "Filter the adventure…" : "Filter rules and tables…");
        ImGui::BeginChild("##rules", ImVec2(0, 0));
        if (filter_.empty()) {
            for (size_t i : roots_) tree(i);
        } else {
            const std::string want = lowered(filter_);
            for (const auto& [id, i] : at_) {
                const RuleNode& n = nodes_[i];
                if (matches(n, want))
                    if (ImGui::Selectable((labelOf(n) + "##" + std::to_string(n.id)).c_str(), n.id == selected_)) selected_ = n.id;
            }
        }
        reveal_.clear();
        scrollToSelected_ = false;
        ImGui::EndChild();
    }

    void drawDetail() override {
        const auto it = at_.find(selected_);
        if (it == at_.end()) {
            ImGui::Spacing();
            ImGui::TextWrapped("Nothing here.");
            return;
        }
        const RuleNode& n = nodes_[it->second];
        bigText(labelOf(n).c_str(), 1.5f, kAccent);
        std::string path;                                       // where it sits: Chapter > Section
        for (int p = n.parent; at_.count(p);) {
            const RuleNode& up = nodes_[at_[p]];
            path = labelOf(up) + (path.empty() ? "" : " > " + path);
            p = up.parent;
        }
        if (!path.empty()) ImGui::TextColored(kGrey, "%s", path.c_str());
        sourceBadge(host_, n.sourceId);
        if (!n.editedBy.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kGold, "Changed by %s", n.editedBy.c_str());
        }
        ImGui::Separator();
        ImGui::BeginChild("##rulebody", ImVec2(0, 0));
        if (shown_ != selected_) {                              // another rule: read it from the top
            ImGui::SetScrollY(0);
            shown_ = selected_;
        }
        paragraphs(n.body, "body");
        for (size_t i = 0; i < n.sections.size(); ++i) {
            const RuleNode::Section& sec = n.sections[i];
            ImGui::Spacing();
            bigText(sec.title.c_str(), 1.1f, kGold);
            paragraphs(sec.body, ("sec" + std::to_string(i)).c_str());
        }
        for (int tableId : host_.content().tablesOfRule(n.id))
            if (const DataTable* t = host_.content().packTable(tableId)) drawTable(*t);
        drawSee(n);
        if (!children_[it->second].empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kGold, "Inside");
            for (size_t c : children_[it->second])
                if (ImGui::SmallButton((labelOf(nodes_[c]) + "##" + std::to_string(nodes_[c].id)).c_str())) show(nodes_[c].id);
        }
        ImGui::Spacing();
        pageLink(host_, n.sourceId, n.ref, n.pageNote);
        if (focusTable_ && --focusFrames_ <= 0) focusTable_ = 0;
        ImGui::EndChild();
    }

    bool wantsRedraw() const override { return focusTable_ != 0; }

private:
    struct Roll {
        int value = 0, row = -1;
    };

    // What the tree and the headings show: the title, with its number in front when the rule is a step ("4. Age").
    static std::string labelOf(const RuleNode& n) {
        const std::string step = n.prop("step");
        return step.empty() ? n.title : step + ". " + n.title;
    }

    bool matches(const RuleNode& n, const std::string& want) const {
        if (lowered(n.title).find(want) != std::string::npos || lowered(n.body).find(want) != std::string::npos) return true;
        for (const RuleNode::Section& sec : n.sections)
            if (lowered(sec.title).find(want) != std::string::npos || lowered(sec.body).find(want) != std::string::npos) return true;
        for (int tableId : host_.content().tablesOfRule(n.id))
            if (const DataTable* t = host_.content().packTable(tableId))
                if (lowered(t->title).find(want) != std::string::npos) return true;
        return false;
    }

    // The module that lists a kind of entries, for a "see" that points at a whole category.
    static const char* moduleFor(Kind k) {
        switch (k) {
            case Kind::Kin: return "kin";
            case Kind::Profession: return "professions";
            case Kind::Skill: return "skills";
            case Kind::Ability: return "abilities";
            case Kind::Spell: return "spells";
            case Kind::Weapon:
            case Kind::Armor:
            case Kind::Gear: return "gear";
            case Kind::Monster: return "creatures";
            case Kind::Table: return "rules";
        }
        return "rules";
    }

    // "See also": what the rule points to in the rest of the data (the entries of a category, one entry, another rule), as buttons that go there.
    void drawSee(const RuleNode& n) {
        std::vector<SeeTarget> targets;
        for (const std::string& ref : n.see)
            if (SeeTarget t = host_.content().seeTarget(ref); t.type != SeeTarget::Type::None) targets.push_back(std::move(t));
        if (targets.empty()) return;
        ImGui::Spacing();
        ImGui::TextColored(kGold, "See also");
        const float width = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
        float used = 0;
        for (size_t i = 0; i < targets.size(); ++i) {
            const SeeTarget& t = targets[i];
            const float w = ImGui::CalcTextSize(t.label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i > 0 && used + gap + w <= width) ImGui::SameLine();
            else used = 0;
            used += (used > 0 ? gap : 0) + w;
            const bool there = t.type == SeeTarget::Type::Rule || host_.kindAvailable(t.kind);      // (the module that shows it may be off)
            ImGui::BeginDisabled(!there);
            if (ImGui::SmallButton((t.label + "##see" + std::to_string(i)).c_str())) {
                if (t.type == SeeTarget::Type::Rule) show(t.id);
                else if (t.type == SeeTarget::Type::Entry) host_.goTo(t.kind, t.id);
                else host_.showModule(moduleFor(t.kind));
            }
            ImGui::EndDisabled();
        }
    }

    // Select a rule and make the list show it: its parents open and the list scrolls to it.
    void show(int ruleId) {
        selected_ = ruleId;
        filter_.clear();
        reveal_.clear();
        for (int p = nodes_[at_[ruleId]].parent; at_.count(p); p = nodes_[at_[p]].parent) reveal_.insert(p);
        scrollToSelected_ = true;
    }

    void drawTable(const DataTable& t) {
        ImGui::PushID(t.id);
        ImGui::Spacing();
        ImGui::Spacing();
        if (t.id == focusTable_) ImGui::SetScrollHereY(0.05f);                   // reached from a search or a link: bring it into view
        const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        bigText(t.title.c_str(), 1.25f, kAccent);
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "· %s", t.dice.empty() ? "table" : (t.dice + " table").c_str());
        if (IMasterScreen* screen = serviceOf<IMasterScreen>(host_)) {
            const float btnW = ImGui::CalcTextSize("Pin").x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SameLine(rightEdge - btnW);
            if (ImGui::SmallButton("Pin")) screen->pinEntry(Kind::Table, t.id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pin this table to the Master Screen");
        }
        const SourceInfo* src = host_.content().source(t.sourceId);
        Roll& roll = rolls_[t.id];
        if (const int sides = t.dieSides(); sides > 0) {
            if (ImGui::Button(("Roll " + t.dice).c_str())) {
                roll.value = host_.dice().roll(sides);
                roll.row = t.rowForRoll(roll.value);
                host_.flashRoll();
            }
            if (roll.value > 0) {
                ImGui::SameLine();
                ImGui::TextColored(kGold, "Rolled %d", roll.value);
            }
        }
        ImGui::Spacing();
        tableGrid(t, roll.row);
        if (t.ref.valid() || !(src && src->homebrew)) pageLink(host_, t.sourceId, t.ref, t.pageNote);
        else if (!t.pageNote.empty()) ImGui::TextColored(kGrey, "%s", t.pageNote.c_str());
        ImGui::PopID();
    }

    void tree(size_t i) {
        const RuleNode& n = nodes_[i];
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children_[i].empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (n.id == selected_) flags |= ImGuiTreeNodeFlags_Selected;
        if (reveal_.count(n.id)) ImGui::SetNextItemOpen(true);
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(n.id)), flags, "%s", labelOf(n).c_str());
        if (n.id == selected_ && scrollToSelected_) ImGui::SetScrollHereY(0.3f);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selected_ = n.id;
        if (open && !children_[i].empty()) {
            for (size_t c : children_[i]) tree(c);
            ImGui::TreePop();
        }
    }

    const bool adventures_;                                   // false: this instance is "Rules"; true: "Adventures"
    std::vector<RuleNode> nodes_;                              // every rule of every pack (shared; at_/children_/roots_ pick this module's share)
    std::unordered_map<int, size_t> at_;                       // rule id -> index in nodes_, for the rules this module shows
    std::vector<std::vector<size_t>> children_;                // indexed like nodes_; only entries for ids in at_ are populated
    std::vector<size_t> roots_;
    std::unordered_map<int, Roll> rolls_;                     // per table: the last roll, so the row stays highlighted
    std::unordered_set<int> reveal_;                          // rules whose tree node must be open (the parents of what was just shown)
    std::string filter_;
    int selected_ = 0;
    int shown_ = 0;                                           // the rule the detail was drawn for last
    int focusTable_ = 0;                                      // a table to scroll into view when the detail draws it
    int focusFrames_ = 0;                                     // for how many more frames
    bool scrollToSelected_ = false;
};

}  // namespace

std::unique_ptr<Module> makeRulesModule(Host& host) { return std::make_unique<RulesModule>(host, false); }
std::unique_ptr<Module> makeAdventuresModule(Host& host) { return std::make_unique<RulesModule>(host, true); }

}  // namespace gm
