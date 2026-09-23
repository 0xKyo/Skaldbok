// Settings: which modules are on, which content packs are loaded (and importing homebrew), and where things live.
#include <cstdio>
#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "filedialog.h"
#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

std::string fileUrl(std::string path) {
    std::string url = "file:///";
    for (char c : path) {
        if (c == '\\') url += '/';
        else if (c == ' ') url += "%20";
        else url += c;
    }
    return url;
}

class SettingsModule : public Module {
public:
    explicit SettingsModule(Host& host) : Module(host) {}

    const char* id() const override { return "settings"; }
    const char* title() const override { return "Settings"; }
    const char* summary() const override { return "Switch modules and content packs on and off, import homebrew."; }
    bool required() const override { return true; }
    bool showInNav() const override { return false; }             // reached with the gear button, not the sidebar
    Layout layout() const override { return Layout::Full; }
    bool wantsRedraw() const override { return dialog_.busy(); }
    void showChild(const std::string& moduleId) override { focus_ = moduleId; }

    void update() override {
        std::string path;
        if (!dialog_.poll(path) || path.empty()) return;
        const ImportResult r = host_.packManager().import(path);
        result_ = r;
        haveResult_ = true;
        if (r.ok) {
            host_.settings().setPackEnabled(r.packId, true);         // an imported pack starts on
            host_.contentChanged();
        }
        host_.notify(r.message);
    }

    // One tab for the settings themselves, and one for each module that lives here (Web).
    void drawFull() override {
        bigText("Settings", 1.6f, kAccent);
        if (!ImGui::BeginTabBar("##settingstabs")) return;
        if (ImGui::BeginTabItem("General", nullptr, focus_ == "general" ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawGeneral();
            ImGui::EndTabItem();
        }
        for (const auto& m : host_.modules()) {
            if (m.get() == this || std::string(m->hostedBy()) != id() || !moduleIsOn(host_, *m)) continue;
            if (ImGui::BeginTabItem(m->title(), nullptr, focus_ == m->id() ? ImGuiTabItemFlags_SetSelected : 0)) {
                m->drawFull();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        focus_.clear();
    }

    void drawGeneral() {
        ImGui::BeginChild("##settings", ImVec2(0, 0));

        // ---- modules ------------------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Modules", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(kGrey, "A module that is off disappears from the sidebar and from search; nothing else is affected.");
            if (ImGui::BeginTable("##modules", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, U(40));
                ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthFixed, U(150));
                ImGui::TableSetupColumn("What it does", ImGuiTableColumnFlags_WidthStretch);
                for (const auto& m : host_.modules()) {
                    ImGui::TableNextRow();
                    ImGui::PushID(m->id());
                    ImGui::TableSetColumnIndex(0);
                    bool on = moduleIsOn(host_, *m);
                    ImGui::BeginDisabled(m->required());
                    if (ImGui::Checkbox("##on", &on)) {
                        host_.settings().setModuleEnabled(m->id(), on);
                        m->enabledChanged(on);
                        host_.contentChanged();              // lists and search results depend on which modules are on
                    }
                    ImGui::EndDisabled();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(m->title());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextColored(kGrey, "%s%s", m->summary(), m->required() ? " (always on)" : "");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        // ---- content packs -----------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Content packs", ImGuiTreeNodeFlags_DefaultOpen)) drawPacks();

        // ---- about --------------------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Where things are")) {
            const Paths& p = host_.paths();
            auto line = [&](const char* label, const std::string& value, bool openable) {
                ImGui::TextColored(kGold, "%s", label);
                ImGui::SameLine(U(170));
                ImGui::TextUnformatted(value.c_str());
                if (openable) {
                    ImGui::SameLine();
                    ImGui::PushID(label);
                    if (ImGui::SmallButton("Open")) SDL_OpenURL(fileUrl(value).c_str());
                    ImGui::PopID();
                }
            };
            line("Books & Core pack", p.dataDir, true);
            line("Imported packs", p.userPacksDir(), true);
            line("Characters", p.charactersDir(), true);
            line("Settings, saves", p.prefDir, true);
        }
        ImGui::EndChild();
    }

private:
    void drawPacks() {
        ContentStore& content = host_.content();
        ImGui::TextWrapped("A content pack is a folder (or a .zip of one) with a manifest.json and one JSON file per type: creatures, spells, abilities, "
                           "skills, kin, professions, weapons, armor, gear, tables. The Core pack holds the books' content; homebrew packs add to it and "
                           "keep their own source (\"Homebrew · name\"). See docs/HOMEBREW.md for the format and examples/frostmarch-tales for a sample.");
        ImGui::BeginDisabled(dialog_.busy());
        if (ImGui::Button("Import a pack folder…")) dialog_.openFolder(host_.window());
        ImGui::SameLine();
        if (ImGui::Button("Import a .zip…")) dialog_.openFile(host_.window(), "Content pack (zip)", "zip");
        ImGui::EndDisabled();
        if (haveResult_) {
            ImGui::TextColored(result_.ok ? kAccent : kRed, "%s", result_.message.c_str());
            for (const std::string& w : result_.warnings) ImGui::TextColored(kGold, "  · %s", w.c_str());
        }
        ImGui::Spacing();

        bool reload = false;
        std::string removeId;
        for (const PackInfo& p : content.packs()) {
            ImGui::PushID(p.dir.c_str());
            ImGui::Separator();
            const ImVec4 col = p.loaded ? kAccent : !p.error.empty() ? kRed : kGrey;
            bool on = p.enabled;
            ImGui::BeginDisabled(p.core);
            if (ImGui::Checkbox("##on", &on)) {
                host_.settings().setPackEnabled(p.id, on);
                reload = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(col, "%s", p.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "%s%s%s%s", p.version.empty() ? "" : ("v" + p.version).c_str(), p.author.empty() ? "" : "  by ",
                               p.author.c_str(), p.core ? "  (built in)" : "");
            if (!p.core) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) ImGui::OpenPopup("Remove this pack?");
                if (ImGui::BeginPopupModal("Remove this pack?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("\"%s\" and its files will be deleted from the imported packs folder.", p.name.c_str());
                    ImGui::TextColored(kGrey, "Characters that use it keep working: they remember the names.");
                    if (ImGui::Button("Remove")) {
                        removeId = p.id.empty() ? std::string() : p.id;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Keep")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            if (!p.description.empty()) ImGui::TextColored(kGrey, "%s", p.description.c_str());
            if (!p.error.empty()) {
                ImGui::TextColored(kRed, "Cannot be used: %s", p.error.c_str());
            } else if (!p.enabled) {
                ImGui::TextColored(kGrey, "Off: nothing of it is loaded.");
            } else {
                std::string counts = p.rules > 0 ? std::to_string(p.rules) + " rules" : std::string();
                static const struct {
                    Kind kind;
                    const char* name;
                } names[] = {{Kind::Monster, "creatures"}, {Kind::Spell, "spells"},       {Kind::Ability, "abilities"}, {Kind::Skill, "skills"},
                             {Kind::Kin, "kin"},           {Kind::Profession, "professions"}, {Kind::Weapon, "weapons"},  {Kind::Armor, "armor"},
                             {Kind::Gear, "gear items"},   {Kind::Table, "tables"}};
                for (const auto& n : names)
                    if (p.counts[static_cast<int>(n.kind)] > 0)
                        counts += (counts.empty() ? "" : ", ") + std::to_string(p.counts[static_cast<int>(n.kind)]) + " " + n.name;
                ImGui::TextColored(kGrey, "%s", counts.empty() ? "Nothing in it." : counts.c_str());
            }
            if (!p.warnings.empty() && ImGui::TreeNode("##warn", "%d warning(s)", static_cast<int>(p.warnings.size()))) {
                for (const std::string& w : p.warnings) ImGui::TextColored(kGold, "· %s", w.c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (!removeId.empty()) {
            std::string err;
            if (host_.packManager().remove(removeId, &err)) {
                host_.settings().setPackEnabled(removeId, true);      // forget that it was off
                host_.notify("Pack removed");
            } else {
                host_.notify(err);
            }
            reload = true;
        }
        if (reload) host_.contentChanged();
    }

    FileDialog dialog_;
    ImportResult result_;
    bool haveResult_ = false;
    std::string focus_;                                      // the tab to bring to the front on the next frame
};

}  // namespace

std::unique_ptr<Module> makeSettingsModule(Host& host) { return std::make_unique<SettingsModule>(host); }

}  // namespace gm
