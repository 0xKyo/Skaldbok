// Search: one box, every book and every pack. The box itself lives in the shell's top bar; this module owns the query and the results.
#include <imgui.h>

#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

class SearchModule : public Module, public ISearch {
public:
    explicit SearchModule(Host& host) : Module(host) {}

    const char* id() const override { return "search"; }
    const char* title() const override { return "Search"; }
    const char* summary() const override { return "Full-text search over creatures, rules, the books' tables and every content pack (Ctrl+K)."; }
    bool required() const override { return true; }

    char* queryBuffer() override { return buf_; }
    size_t queryCapacity() const override { return sizeof buf_; }
    void runQuery() override {
        last_ = buf_;
        hits_.clear();
        for (Hit& h : host_.content().search(last_, 80))
            if (host_.kindAvailable(h.kind)) hits_.push_back(std::move(h));       // hits of a module that is off are hidden
    }

    void onContentChanged() override {
        if (!last_.empty()) runQuery();
    }

    void drawList() override {
        ImGui::TextColored(ui::kGrey, "Full-text search across creatures, rules, tables and content packs. Ctrl+K to focus.");
        ImGui::BeginChild("##hits", ImVec2(0, 0));
        if (hits_.empty()) ImGui::TextColored(ui::kGrey, last_.empty() ? "Type in the search box above." : "Nothing found.");
        const Selection* sel = host_.selection();
        for (size_t i = 0; i < hits_.size(); ++i) {
            const Hit& h = hits_[i];
            ImGui::PushID(static_cast<int>(i));
            const bool selected = sel && sel->kind == h.kind && sel->id == h.id;
            const ImVec2 start = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##hit", selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, ui::lineH() * 3.2f))) host_.goTo(h.kind, h.id);
            ImGui::SetCursorScreenPos(ImVec2(start.x + 6, start.y + 2));
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::sourceColor(host_.content(), h.sourceId));
            ImGui::TextUnformatted(kindLabel(h.kind));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(h.title.c_str());
            ui::highlighted(h.snippet);
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

private:
    char buf_[128] = {};
    std::string last_;
    std::vector<Hit> hits_;
};

}  // namespace

std::unique_ptr<Module> makeSearchModule(Host& host) { return std::make_unique<SearchModule>(host); }

}  // namespace gm
