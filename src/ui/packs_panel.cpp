#include "ui/packs_panel.h"

#include <string>

#include <imgui.h>

#include "parsing/i18n.h"
#include "ui/ui_common.h"

namespace gm {

using namespace ui;
using i18n::tr;

void PacksPanel::update() {
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

void PacksPanel::draw() {
    ContentStore& content = host_.content();
    ImGui::TextWrapped("%s", tr("A content pack is a folder (or a .zip of one) with a manifest.json and one JSON file per type: creatures, spells, abilities, "
                                "skills, kin, professions, weapons, armor, gear, tables. The Core pack holds the books' content; homebrew packs add to it and "
                                "keep their own source (\"Homebrew · name\"). See docs/HOMEBREW.md for the format and docs/examples/frostmarch-tales for a sample.").c_str());
    ImGui::BeginDisabled(dialog_.busy());
    if (ImGui::Button((tr("Import a pack folder…") + "###import-folder").c_str())) dialog_.openFolder(host_.window());
    ImGui::SameLine();
    if (ImGui::Button((tr("Import a .zip…") + "###import-zip").c_str())) dialog_.openFile(host_.window(), tr("Content pack (zip)").c_str(), "zip");
    ImGui::EndDisabled();
    if (haveResult_) {
        ImGui::TextColored(result_.ok ? kAccent : kRed, "%s", tr(result_.message).c_str());
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
        ImGui::TextColored(kGrey, "%s%s%s%s", p.version.empty() ? "" : ("v" + p.version).c_str(), p.author.empty() ? "" : ("  " + tr("by") + " ").c_str(),
                           p.author.c_str(), p.core ? ("  (" + tr("built in") + ")").c_str() : "");
        if (!p.core) {
            ImGui::SameLine();
            if (ImGui::SmallButton((tr("Remove") + "###remove").c_str())) ImGui::OpenPopup("###remove-pack");
            if (ImGui::BeginPopupModal((tr("Remove this pack?") + "###remove-pack").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted(tr("\"" + p.name + "\" and its files will be deleted from the imported packs folder.").c_str());
                ImGui::TextColored(kGrey, "%s", tr("Characters that use it keep working: they remember the names.").c_str());
                if (ImGui::Button((tr("Remove") + "###remove-yes").c_str())) {
                    removeId = p.id.empty() ? std::string() : p.id;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button((tr("Keep") + "###remove-no").c_str())) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        if (!p.description.empty()) ImGui::TextColored(kGrey, "%s", p.description.c_str());
        if (!p.error.empty()) {
            ImGui::TextColored(kRed, "%s", (tr("Cannot be used:") + " " + p.error).c_str());
        } else if (!p.enabled) {
            ImGui::TextColored(kGrey, "%s", tr("Off: nothing of it is loaded.").c_str());
        } else {
            std::string counts = p.rules > 0 ? std::to_string(p.rules) + " " + tr("rules") : std::string();
            for (int k = 0; k < kKindCount; ++k)
                if (p.counts[k] > 0)
                    counts += (counts.empty() ? "" : ", ") + std::to_string(p.counts[k]) + " " + lowered(tr(kindTitle(static_cast<Kind>(k))));
            ImGui::TextColored(kGrey, "%s", counts.empty() ? tr("Nothing in it.").c_str() : counts.c_str());
        }
        if (!p.warnings.empty() && ImGui::TreeNode("##warn", "%s", tr(std::to_string(p.warnings.size()) + " warning(s)").c_str())) {
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

}  // namespace gm
