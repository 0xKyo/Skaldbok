// Rules: the text of the books that is not already a creature, spell, skill, kin, profession or table, kept in one place
// so it can be reviewed and trimmed down to what is worth having.
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

class RulesModule : public Module {
public:
    explicit RulesModule(Host& host) : Module(host) { onContentChanged(); }

    const char* id() const override { return "rules"; }
    const char* title() const override { return "Rules"; }
    const char* summary() const override { return "What is left of the books' text once creatures, spells, skills, kin, tables... have their own pages."; }
    const char* group() const override { return "Reference"; }
    bool ownsDetail() const override { return true; }
    int badge() const override { return static_cast<int>(nodes_.size()); }

    void onContentChanged() override {
        nodes_ = host_.db().listRules();
        at_.clear();
        children_.assign(nodes_.size(), {});
        roots_.clear();
        for (size_t i = 0; i < nodes_.size(); ++i) at_[nodes_[i].id] = i;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto p = at_.find(nodes_[i].parent);
            (p == at_.end() ? roots_ : children_[p->second]).push_back(i);
        }
        if (!at_.count(selected_)) selected_ = nodes_.empty() ? 0 : nodes_.front().id;
    }

    void drawList() override {
        inputStr("##rulefilter", filter_, -FLT_MIN, "Filter rules…");
        ImGui::BeginChild("##rules", ImVec2(0, 0));
        if (filter_.empty()) {
            for (size_t i : roots_) tree(i);
        } else {
            const std::string want = lowered(filter_);
            for (const RuleNode& n : nodes_)
                if (lowered(n.title).find(want) != std::string::npos || lowered(n.body).find(want) != std::string::npos)
                    if (ImGui::Selectable((n.title + "##" + std::to_string(n.id)).c_str(), n.id == selected_)) selected_ = n.id;
        }
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
        bigText(n.title.c_str(), 1.5f, kAccent);
        std::string path;                                       // where it sits: Chapter > Section
        for (int p = n.parent; p != 0;) {
            const RuleNode& up = nodes_[at_[p]];
            path = up.title + (path.empty() ? "" : " > " + path);
            p = up.parent;
        }
        if (!path.empty()) ImGui::TextColored(kGrey, "%s", path.c_str());
        sourceBadge(host_, n.ref.sourceId);
        ImGui::SameLine();
        pageLink(host_, n.ref.sourceId, n.ref, "");
        ImGui::Separator();
        ImGui::BeginChild("##rulebody", ImVec2(0, 0));
        paragraphs(n.body);
        if (!children_[it->second].empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kGold, "Inside");
            for (size_t c : children_[it->second])
                if (ImGui::SmallButton((nodes_[c].title + "##" + std::to_string(nodes_[c].id)).c_str())) selected_ = nodes_[c].id;
        }
        ImGui::EndChild();
    }

private:
    void tree(size_t i) {
        const RuleNode& n = nodes_[i];
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children_[i].empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (n.id == selected_) flags |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(n.id)), flags, "%s", n.title.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selected_ = n.id;
        if (open && !children_[i].empty()) {
            for (size_t c : children_[i]) tree(c);
            ImGui::TreePop();
        }
    }

    std::vector<RuleNode> nodes_;
    std::unordered_map<int, size_t> at_;                      // section id -> index in nodes_
    std::vector<std::vector<size_t>> children_;
    std::vector<size_t> roots_;
    std::string filter_;
    int selected_ = 0;
};

}  // namespace

std::unique_ptr<Module> makeRulesModule(Host& host) { return std::make_unique<RulesModule>(host); }

}  // namespace gm
