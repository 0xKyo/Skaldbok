#include "ui/app.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>   // SetKeyOwner: the shell keeps Tab for itself

#include "game/creation.h"
#include "parsing/fsutil.h"
#include "game/sheet_edit.h"
#include "ui/icon_data.h"
#include "ui/modules/modules.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

double nowSeconds() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }

}  // namespace

// ------------------------------------------------------------------------------------- setup

App::App(SDL_Window* window, SDL_Renderer* renderer, Paths paths)
    : window_(window), renderer_(renderer), paths_(std::move(paths)), textures_(renderer) {
    ui::setLinkHost(this);                                 // the links in texts open through the shell
    // the per-user folder: --prefs <dir> for tests, otherwise the system's
    std::string prefDir = paths_.prefDir;
    if (prefDir.empty()) {
        if (char* pref = SDL_GetPrefPath("skaldbok", "gm")) {
            prefDir = pref;
            SDL_free(pref);
        }
    } else {
        SDL_CreateDirectory(prefDir.c_str());
    }
    if (prefDir.empty()) {                                 // the system gave no per-user folder: keep things next to the data
        prefDir = paths_.dataDir + "/user";
        SDL_CreateDirectory(prefDir.c_str());
    }
    for (char& c : prefDir)
        if (c == '\\') c = '/';
    if (!prefDir.empty() && prefDir.back() != '/') prefDir += '/';
    paths_.prefDir = prefDir;
    if (!prefDir.empty()) {
        SDL_CreateDirectory(paths_.userPacksDir().c_str());
        settings_.open(prefDir + "settings.json");
        recentFile_ = prefDir + "recent.txt";
        characters_.setDir(paths_.charactersDir());
        parties_.setDir(paths_.partiesDir());
        messages_.setPrefDir(prefDir);
        messages_.setFreshMs(250);
        changes_.setPrefDir(prefDir);
    }
    packs_ = std::make_unique<PackManager>(paths_.coreDir(), paths_.userPacksDir());

    setupStyle();
    gearIcon_ = textureFromMemory(renderer_, kSettingsPng, kSettingsPngSize);
    loadContent();
    modules_ = createModules(*this);
    for (const auto& m : modules_)
        if (m->id() == std::string("search")) active_ = m.get();
    loadRecents();
}

App::~App() {
    modules_.clear();                                      // (before the texture the modules never touch, and the rest of the members)
    if (gearIcon_) SDL_DestroyTexture(gearIcon_);
}

void App::loadContent() {
    const std::vector<PackSpec> specs = packs_->specs(settings_.disabledPacks());
    packSig_ = packSignature(specs);                       // taken before reading: a file that changes meanwhile is noticed next time
    pendingSig_.clear();
    content_.load(specs);
}

// Packs were imported, removed or switched, or a module was: read everything again and tell the modules.
void App::reloadContent() {
    reloadRequested_ = false;
    const bool fromDisk = std::exchange(reloadFromDisk_, false);
    Selection old = sel_;
    const std::string oldKey = hasSel_ ? content_.keyOf(old.kind, old.id) : std::string();
    loadContent();
    textures_.clear();                                     // art may have been replaced or added
    history_.clear();
    historyPos_ = -1;
    if (hasSel_) {                                         // the same entry may have a new handle now, or be gone
        const int id = oldKey.empty() ? 0 : content_.idByKey(old.kind, oldKey);
        if (id && handlerFor(old.kind, id)) sel_.id = id;
        else hasSel_ = false;
    }
    for (const auto& m : modules_) m->onContentChanged();
    if (hasSel_)
        if (Module* h = handlerFor(sel_.kind, sel_.id)) h->onSelect(sel_);
    if (fromDisk) {
        for (const PackInfo& p : content_.packs())
            if (!p.error.empty()) {
                notify("Pack " + (p.name.empty() ? p.dir : p.name) + ": " + p.error.substr(0, 140));
                return;
            }
        notify("Content reloaded");
    }
}

Module* App::moduleById(const std::string& id) const {
    for (const auto& m : modules_)
        if (id == m->id()) return m.get();
    return nullptr;
}

void App::showModule(const std::string& id) {
    Module* m = moduleById(id);
    if (!m || m == active_) return;
    if (std::string(m->id()) == "web") beforeServer_ = active_;   // the gear goes back to this
    active_ = m;
}

void App::openLink(const std::string& target) {
    if (target.starts_with("http://") || target.starts_with("https://")) {
        SDL_OpenURL(target.c_str());
        return;
    }
    const SeeTarget t = content_.resolveLink(target);
    Place p;
    switch (t.type) {
        case SeeTarget::Type::Entry: navigate(t.kind, t.id, true); return;          // (navigate writes the history)
        case SeeTarget::Type::Category:
            showModule(kindPage(t.kind));
            p.module = kindPage(t.kind);
            p.label = kindTitle(t.kind);
            break;
        case SeeTarget::Type::Rule: {
            openRule(*this, t.id, t.section, t.line);
            const RuleNode* r = content_.rule(t.id);
            p.rule = t.id;
            p.ruleSection = t.section;
            p.line = t.line;
            p.label = r ? r->title : t.label;
            if (r && t.section >= 0 && t.section < static_cast<int>(r->sections.size())) p.label += " › " + r->sections[static_cast<size_t>(t.section)].title;
            p.module = "rules";
            break;
        }
        case SeeTarget::Type::Section:
            ui::openIntroSection(*this, t.kind, t.id, t.line);
            p.module = kindPage(t.kind);
            p.introSection = true;
            p.introKind = t.kind;
            p.section = t.id;
            p.line = t.line;
            p.label = std::string(kindTitle(t.kind)) + " › " + t.label;
            break;
        case SeeTarget::Type::None: notify("Nothing to open for \"" + target + "\""); return;
    }
    pushPlace(std::move(p));
}

// The gear: opens the web server page (the only settings there are), and closes it again (back to what was open before).
void App::toggleServerPage() {
    if (active_ && std::string(active_->id()) == "web") {
        if (beforeServer_ && beforeServer_ != active_) active_ = beforeServer_;
        else showModule("search");
    } else {
        showModule("web");
    }
}

Module* App::handlerFor(Kind k, int id) const {
    for (const auto& m : modules_)
        if (m->handles(k, id)) return m.get();
    return nullptr;
}

bool App::kindAvailable(Kind k) const { return handlerFor(k) != nullptr; }

// -------------------------------------------------------------------------------- navigation

void App::navigate(Kind kind, int id, bool pushHistory) {
    Module* h = handlerFor(kind, id);
    if (!h) {
        notify(std::string("No page shows ") + kindLabel(kind) + " entries");
        return;
    }
    const Selection s{kind, id};
    sel_ = s;
    hasSel_ = true;
    active_ = h;
    if (pushHistory) {
        Place p;
        p.module = h->id();
        p.entry = true;
        p.sel = s;
        p.label = content_.titleOf(kind, id);
        pushPlace(std::move(p));
    }
    h->onSelect(s);
    noteRecent(s);
}

void App::pushPlace(Place p) {
    recorded_ = active_;
    if (historyPos_ >= 0 && history_[static_cast<size_t>(historyPos_)] == p) return;
    history_.resize(static_cast<size_t>(historyPos_ + 1));
    history_.push_back(std::move(p));
    if (history_.size() > 100) history_.erase(history_.begin());
    historyPos_ = static_cast<int>(history_.size()) - 1;
}

void App::showPlace(const Place& p) {
    if (p.entry) {
        navigate(p.sel.kind, p.sel.id, false);
    } else if (p.rule) {
        openRule(*this, p.rule, p.ruleSection, p.line);
    } else if (p.introSection) {
        ui::openIntroSection(*this, p.introKind, p.section, p.line);
    } else {
        showModule(p.module);
    }
    recorded_ = active_;
}

void App::goToHistory(int pos) {
    if (pos < 0 || pos >= static_cast<int>(history_.size())) return;
    historyPos_ = pos;
    showPlace(history_[static_cast<size_t>(pos)]);
}

void App::goBack() { goToHistory(historyPos_ - 1); }

void App::goForward() { goToHistory(historyPos_ + 1); }

// ---------------------------------------------------------------------------- saved state

// One "<kind>\t<key>" per line. Unknown or malformed lines are ignored, so a damaged file never blocks startup.
void App::loadRecents() {
    recents_.clear();
    if (recentFile_.empty()) return;
    const auto found = fs::readFile(recentFile_);
    if (!found) return;
    const std::string& text = *found;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        const size_t tab = line.find('\t');
        Kind k = Kind::Monster;
        if (tab != std::string::npos && kindFromKey(line.substr(0, tab), k)) recents_.push_back({k, line.substr(tab + 1)});
        pos = end + 1;
    }
}

void App::saveRecents() const {
    if (recentFile_.empty()) return;
    std::string text;
    for (const Recent& r : recents_) text += std::string(kindKey(r.kind)) + "\t" + r.key + "\n";
    fs::writeFile(recentFile_, text);
}

void App::noteRecent(const Selection& s) {
    const std::string key = content_.keyOf(s.kind, s.id);
    if (key.empty()) return;
    std::erase_if(recents_, [&](const Recent& r) { return r.kind == s.kind && r.key == key; });
    recents_.insert(recents_.begin(), {s.kind, key});
    if (recents_.size() > 12) recents_.resize(12);
    saveRecents();
}

std::string App::nameOf(const Recent& r) {
    const std::string cacheKey = std::string(kindKey(r.kind)) + r.key;
    auto it = nameCache_.find(cacheKey);
    if (it != nameCache_.end()) return it->second;
    std::string name;
    const int id = content_.idByKey(r.kind, r.key);
    if (id) name = content_.titleOf(r.kind, id);
    return nameCache_[cacheKey] = name;
}

void App::notify(const std::string& text) {
    toast_ = text;
    toastUntil_ = nowSeconds() + 3.5;
}

// ------------------------------------------------------------------------------------ dice

void App::flashRoll() { rollFlashUntil_ = nowSeconds() + 1.2; }

void App::dropFile(const std::string& path, float x, float y) {
    if (IMasterScreen* screen = serviceOf<IMasterScreen>(*this)) screen->pinImage(path, x, y);
}

void App::rollExpression(const std::string& text, const std::string& label) {
    auto e = parseDice(text);
    if (!e) {
        notify("'" + text + "' is not a dice expression (try 2D8+3)");
        return;
    }
    rolls_.push_front(dice_.roll(*e, label));
    if (rolls_.size() > 12) rolls_.pop_back();
    flashRoll();
}

bool App::pollExternal() {
    const double now = nowSeconds();
    if (now < nextPoll_) return false;
    nextPoll_ = now + 0.3;
    bool any = false;
    if (now >= nextPackPoll_) {                                    // a pack's files were edited: read them again once the edit is over
        nextPackPoll_ = now + 0.6;
        const std::string sig = packSignature(packs_->specs(settings_.disabledPacks()));
        if (sig == packSig_) pendingSig_.clear();
        else if (sig == pendingSig_) {
            reloadRequested_ = reloadFromDisk_ = any = true;
        } else pendingSig_ = sig;
    }
    if (!characters_.poll().empty()) any = true;
    if (!parties_.poll().empty()) any = true;
    for (const std::string& id : messages_.poll()) {               // a conversation changed: tell the GM if the player wrote
        any = true;
        const int unread = messages_.unread(id, true);
        int& known = unreadSeen_[id];
        if (unread > known) {
            const Character* c = characters_.find(id);
            const Thread& t = messages_.thread(id);
            const std::string what = t.messages.empty() ? std::string() : (t.messages.back().text.empty() ? "[picture]" : t.messages.back().text);
            notify((c ? c->name : std::string("A player")) + ": " + what.substr(0, 90));
        }
        known = unread;
    }
    for (const auto& [id, entry] : changes_.poll()) {               // a player edited their sheet
        any = true;
        if (entry.by != "player") continue;
        const Character* c = characters_.find(id);
        notify((c ? c->name : std::string("A player")) + " changed: " + entry.summary.substr(0, 110));
    }
    return any;
}

bool App::wantsRedraw() const {
    const double t = nowSeconds();
    if (t < rollFlashUntil_ || t < toastUntil_ || reloadRequested_) return true;
    for (const auto& m : modules_)
        if (m->wantsRedraw()) return true;
    return false;
}

// --------------------------------------------------------------------------------- startup

void App::applyStartup(const StartupOptions& o) {
    if (!o.importPath.empty()) {
        const ImportResult r = packs_->import(o.importPath);
        std::printf("%s\n", r.message.c_str());
        for (const std::string& w : r.warnings) std::printf("  warning: %s\n", w.c_str());
        if (r.ok) settings_.setPackEnabled(r.packId, true);
        reloadContent();
    }
    std::string tab = o.tab;
    if (tab == "monsters") tab = "creatures";               // the old name
    if (!tab.empty()) showModule(tab);
    if (!o.select.empty()) {
        std::string want = o.select;
        for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        Selection found;
        bool done = false;
        if (active_ && active_->findByName(want, found)) done = true;   // --tab narrows the choice
        for (const auto& m : modules_) {
            if (done || !tab.empty()) break;
            if (m->findByName(want, found)) done = true;
        }
        if (done) navigate(found.kind, found.id, true);
    }
    if (!o.search.empty())
        if (ISearch* s = serviceOf<ISearch>(*this)) {
            std::snprintf(s->queryBuffer(), s->queryCapacity(), "%s", o.search.c_str());
            s->runQuery();
            showModule("search");
        }
    if (o.demoEncounter)
        if (IEncounterSink* e = serviceOf<IEncounterSink>(*this)) {
            e->demoFight(o.demoRoll);
            showModule("encounter");
        }
    if (o.demoCharacter)
        if (ICharacters* c = serviceOf<ICharacters>(*this)) {
            c->demoCharacter();
            showModule("characters");
        }
    if (o.demoParty) {
        Party party;
        party.name = "The Misty Vale party";
        party.notes = "Meets at the inn every Friday. Next: the ruined watchtower.";
        for (int i = 0; i < 3; ++i) {
            Character c = buildCharacter(randomCreation(content_, dice_), content_, dice_);
            c.hp = std::max(1, c.hp - i * 2);
            characters_.save(c);
            party.members.push_back(c.id);
        }
        parties_.save(party);
        // a little life in the chat and on the sheets, as if the players had been at it
        if (party.members.size() >= 2) {
            std::string err;
            const std::string first = party.members[0], second = party.members[1];
            messages_.sendFromGm(std::vector<std::string>(party.members), true, party.name, "The bridge is out. You will have to swim, or find the ferryman.", "", &err);
            messages_.sendFromPlayer(first, "Can I try to sneak past the guards while the others distract them?", "", &err);
            messages_.sendFromGm(std::vector<std::string>{first}, false, "", "Roll Sneaking with a bane: they are alert tonight.", "", &err);
            messages_.sendFromPlayer(first, "Rolled a 7 against my 9. Made it!", "", &err);
            messages_.sendFromPlayer(second, "I forgot my rope in town. Do I still have the grappling hook?", "", &err);
            json overload = json::array();
            for (int i = 0; i < 11; ++i) overload.push_back({{"name", i == 0 ? "Grappling hook" : "Trade goods " + std::to_string(i)}});
            sheet::editFile(characters_.pathOf(first), first, json{{"hp", 6}, {"inventory", overload}, {"hp_bonus", 3}}, true, &changes_);
            sheet::editFile(characters_.pathOf(second), second, json{{"conditions", json::array({"Scared"})}, {"coins", {{"gold", 12}}}}, true, &changes_);
            characters_.poll();
            if (ICharacters* sheets = serviceOf<ICharacters>(*this)) sheets->openCharacter(first);      // (so --tab characters shows a sheet)
            if (o.tab == "messages")
                if (IMessenger* chat = serviceOf<IMessenger>(*this)) chat->compose("char:" + first, "", "");      // (and --tab messages a conversation)
        }
        showModule(o.tab.empty() ? "party" : o.tab);
    }
    if (o.demoScreen)
        if (IMasterScreen* s = serviceOf<IMasterScreen>(*this)) {
            s->demoBoard();
            showModule("screen");
        }
    if (o.newCharacter >= 0)
        if (ICharacters* c = serviceOf<ICharacters>(*this)) {
            c->startCreator(o.newCharacter);
            showModule("characters");
        }
    if (o.demoRoll) {
        rollExpression("2d8+3", "demo");
        rollExpression("1d20", "demo");
    }
}

// -------------------------------------------------------------------------------------- style

void App::setupStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsLight();
    st.FontSizeBase = 16.0f;
    st.WindowRounding = 0.0f;
    st.ChildRounding = 8.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.PopupRounding = 8.0f;
    st.ScrollbarRounding = 8.0f;
    st.TabRounding = 6.0f;
    st.FramePadding = ImVec2(9, 5);
    st.ItemSpacing = ImVec2(8, 7);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.WindowPadding = ImVec2(12, 10);
    st.ScrollbarSize = 12.0f;
    st.CellPadding = ImVec2(8, 5);
    st.FrameBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;

    // The palette of the printed character sheet: aged cream paper, yellowed parchment, dragon green, the red of the logo, brown ink.
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = kInk;
    c[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.54f, 0.45f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.906f, 0.875f, 0.800f, 1.0f);       // paper
    c[ImGuiCol_ChildBg] = ImVec4(0.941f, 0.910f, 0.824f, 1.0f);        // cream
    c[ImGuiCol_PopupBg] = ImVec4(0.965f, 0.933f, 0.859f, 0.99f);
    c[ImGuiCol_Border] = ImVec4(0.725f, 0.659f, 0.518f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.984f, 0.965f, 0.902f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.957f, 0.922f, 0.812f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.922f, 0.875f, 0.722f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.894f, 0.839f, 0.651f, 1.0f);         // parchment
    c[ImGuiCol_ButtonHovered] = ImVec4(0.847f, 0.773f, 0.541f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.784f, 0.698f, 0.439f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.733f, 0.847f, 0.800f, 1.0f);         // a light dragon green
    c[ImGuiCol_HeaderHovered] = ImVec4(0.659f, 0.804f, 0.745f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.576f, 0.753f, 0.682f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.851f, 0.784f, 0.608f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.725f, 0.659f, 0.518f, 1.0f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.816f, 0.769f, 0.627f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.60f, 0.45f, 0.20f, 0.06f);
    c[ImGuiCol_Separator] = ImVec4(0.725f, 0.659f, 0.518f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.855f, 0.796f, 0.624f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.659f, 0.804f, 0.745f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.498f, 0.722f, 0.647f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = kRed;
    c[ImGuiCol_TabDimmed] = ImVec4(0.878f, 0.827f, 0.682f, 1.0f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.659f, 0.804f, 0.745f, 1.0f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccentDim;
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.878f, 0.835f, 0.722f, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.725f, 0.659f, 0.518f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.627f, 0.561f, 0.408f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.545f, 0.475f, 0.329f, 1.0f);
    c[ImGuiCol_PlotHistogram] = kAccentDim;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.184f, 0.478f, 0.408f, 0.30f);
    c[ImGuiCol_NavCursor] = kAccentDim;
}

// ------------------------------------------------------------------------------ bars / frame

void App::drawSourceChips() {
    const auto chip = [&](const SourceInfo& s, const char* label) {
        const bool on = sourceShown(s.id);
        const ImVec4 col = sourceColor(content_, s.id);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(col.x * 0.9f, col.y * 0.9f, col.z * 0.9f, 1.0f) : ImVec4(0.855f, 0.796f, 0.624f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1, 1, 1, 1) : kGrey);
        ImGui::PushID(s.id);
        const bool clicked = ImGui::Button(label);
        ImGui::PopID();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show or hide content from %s", s.title.c_str());
        return clicked;
    };
    bool homebrew = false;
    for (const SourceInfo& s : content_.sources()) {
        if (!s.book) {
            homebrew = true;
            continue;
        }
        if (chip(s, s.label.c_str())) {
            if (!sourcesOff_.erase(s.id)) sourcesOff_.insert(s.id);
            ++sourceRev_;
        }
        ImGui::SameLine(0, 4);
    }
    if (homebrew) {          // homebrew sources can be many: they share one button and a small menu
        int off = 0, total = 0;
        for (const SourceInfo& s : content_.sources())
            if (!s.book) {
                ++total;
                off += sourcesOff_.count(s.id) ? 1 : 0;
            }
        char label[48];
        std::snprintf(label, sizeof label, "Homebrew %d/%d", total - off, total);
        if (ImGui::Button(label)) ImGui::OpenPopup("##homebrew");
        if (ImGui::BeginPopup("##homebrew")) {
            for (const SourceInfo& s : content_.sources()) {
                if (s.book) continue;
                bool on = sourceShown(s.id);
                ImGui::PushStyleColor(ImGuiCol_CheckMark, sourceColor(content_, s.id));
                if (ImGui::Checkbox(s.label.c_str(), &on)) {
                    if (on) sourcesOff_.erase(s.id);
                    else sourcesOff_.insert(s.id);
                    ++sourceRev_;
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 4);
    }
}

// The gear, at the right end of the top bar: grey at rest, teal on hover and while the web server page is open. The icon is a PNG
// with an alpha channel, so it is drawn straight on the bar with no frame; the tint is what changes.
void App::drawGearButton() {
    const float side = ImGui::GetFrameHeight();
    ImGui::SameLine(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - side));       // flush with the right edge
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool open = active_ && std::string(active_->id()) == "web";
    ImGui::PushID("gear");
    const bool clicked = ImGui::InvisibleButton("##settings", ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || open)
        dl->AddCircleFilled(ImVec2(pos.x + side * 0.5f, pos.y + side * 0.5f), side * 0.5f, ImGui::GetColorU32(ImVec4(0.30f, 0.20f, 0.05f, ImGui::IsItemActive() ? 0.20f : 0.10f)));
    const ImVec4 tint = hovered || open ? kAccent : kGrey;
    const float icon = side * 0.72f, off = (side - icon) * 0.5f;
    if (gearIcon_)
        dl->AddImage(reinterpret_cast<ImTextureID>(gearIcon_), ImVec2(pos.x + off, pos.y + off), ImVec2(pos.x + off + icon, pos.y + off + icon), ImVec2(0, 0), ImVec2(1, 1),
                     ImGui::GetColorU32(tint));
    else
        dl->AddText(ImVec2(pos.x + off, pos.y + off * 0.5f), ImGui::GetColorU32(tint), "Set");     // (the icon could not be decoded)
    if (hovered) ImGui::SetTooltip("Web server");
    if (clicked) toggleServerPage();
}

void App::drawTopBar() {
    // Keyboard focus never lands up here by itself (only the rail and the page take it): the search box is reached
    // with Ctrl+K or a click, the chips and the gear with the mouse. Back / forward are Alt+Left / Alt+Right.
    float chipsW = 0;
    for (const SourceInfo& s : content_.sources())
        if (s.book) chipsW += ImGui::CalcTextSize(s.label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 + 4;
    chipsW += U(130) + ImGui::GetFrameHeight() + 8;            // the Homebrew button, and the gear
    IWebStatus* web = serviceOf<IWebStatus>(*this);
    if (web) chipsW += U(120);                                  // the web server chip
    ISearch* search = serviceOf<ISearch>(*this);
    const float historyW = U(96);
    drawHistoryButtons();
    ImGui::SameLine(0, 8);
    chipsW += historyW;
    if (search) {
        ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - chipsW - U(30)));
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, !focusSearch_ && !searchActive_);   // reachable by Ctrl+K only
        if (focusSearch_) ImGui::SetKeyboardFocusHere();
        focusSearch_ = false;
        const bool edited = ImGui::InputTextWithHint("##search", "Search everything… (Ctrl+K)", search->queryBuffer(), search->queryCapacity());
        ImGui::PopItemFlag();
        searchActive_ = ImGui::IsItemActive();
        if (edited) {
            search->runQuery();
            if (search->queryBuffer()[0]) showModule("search");
        }
    }
    ImGui::SameLine(0, 14);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (web) {                                                  // the players' web server: always visible, one click to its tab
        const bool up = web->webRunning(), starting = web->webStarting();
        ImGui::PushStyleColor(ImGuiCol_Button, up ? kAccent : ImVec4(0.855f, 0.796f, 0.624f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, up ? ImVec4(1, 1, 1, 1) : kGrey);
        if (ImGui::Button(up ? "Web server: on" : starting ? "Web server: starting…" : "Web server: off")) showModule("web");
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(up ? "The players' web page is up. Click for the links." : "The players' web page is off. Click to start it.");
        ImGui::SameLine(0, 4);
    }
    drawSourceChips();
    drawGearButton();
    ImGui::PopItemFlag();
}

// Back, Forward and the list of places visited (newest first; the one you are at is marked).
void App::drawHistoryButtons() {
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool canBack = historyPos_ > 0, canForward = historyPos_ + 1 < static_cast<int>(history_.size());
    ImGui::BeginDisabled(!canBack);
    if (ImGui::Button("<##back")) goBack();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(canBack ? ("Back to " + history_[static_cast<size_t>(historyPos_ - 1)].label + " (Alt+Left)").c_str() : "Back (Alt+Left)");
    ImGui::SameLine(0, 4);
    ImGui::BeginDisabled(!canForward);
    if (ImGui::Button(">##forward")) goForward();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(canForward ? ("Forward to " + history_[static_cast<size_t>(historyPos_ + 1)].label + " (Alt+Right)").c_str() : "Forward (Alt+Right)");
    ImGui::SameLine(0, 4);
    if (ImGui::Button("History")) ImGui::OpenPopup("##history");
    if (ImGui::BeginPopup("##history")) {
        if (history_.empty()) ImGui::TextColored(kGrey, "Nothing visited yet");
        for (int i = static_cast<int>(history_.size()) - 1; i >= 0; --i) {
            const Place& p = history_[static_cast<size_t>(i)];
            const std::string text = (p.label.empty() ? p.module : p.label) + "##h" + std::to_string(i);
            if (ImGui::Selectable(text.c_str(), i == historyPos_)) goToHistory(i);
        }
        ImGui::EndPopup();
    }
    ImGui::PopItemFlag();
}

void App::drawNav() {
    std::string group;
    bool placed = false;                                   // wantFocusNav_ found the open module's entry
    Module* navFocus = nullptr;                            // the entry that has the keyboard highlight this frame
    for (const auto& mp : modules_) {
        Module& m = *mp;
        if (!m.showInNav()) continue;
        if (std::string(m.group()) != group) {
            group = m.group();
            if (!group.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(kGrey, "%s", group.c_str());
            }
        }
        ImGui::PushID(m.id());
        if (wantFocusNav_ && active_ == &m) {              // Tab from the page (or the start) lands on the open module
            ImGui::SetKeyboardFocusHere();
            placed = true;
        }
        const bool clicked = ImGui::Selectable("##nav", active_ == &m, 0, ImVec2(0, lineH() + 12));
        // Arrow keys already move the keyboard highlight between modules (ImGui's own nav); switch to the highlighted
        // one immediately, instead of waiting for Enter/Space to "activate" it. Only when an arrow moved the highlight
        // ONTO this entry: an entry that keeps the highlight, or gets it back (the shell restoring the keyboard to the
        // rail), must not undo a module opened some other way (the gear, a link, the search, history).
        const bool focused = ImGui::IsItemFocused();
        if (focused) navFocus = &m;
        const bool arrowed = ImGui::GetFrameCount() - navArrowFrame_ <= 2;
        if ((clicked || (focused && navFocus_ != &m && arrowed)) && active_ != &m) active_ = &m;
        // a click on Search also puts the cursor in the search box; arrowing past it does not (the keyboard stays here)
        if (clicked && std::string(m.id()) == "search") focusSearch_ = true;
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cy = (mn.y + mx.y) * 0.5f - lineH() * 0.5f;
        dl->AddText(ImVec2(mn.x + 12, cy), ImGui::GetColorU32(ImGuiCol_Text), m.title());
        if (m.badge() >= 0) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d", m.badge());
            const ImVec2 sz = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(mx.x - sz.x - 10, cy), ImGui::GetColorU32(kGrey), buf);
        }
        ImGui::PopID();
    }
    navFocus_ = navFocus;
    if (wantFocusNav_) {
        if (!placed) ImGui::SetWindowFocus();              // the open module has no entry here (the web server page): the rail itself
        ImGui::SetNavCursorVisible(true);                  // show where the keyboard is
    }
    wantFocusNav_ = false;
}

void App::drawDiceBar() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kAccent, "Dice");
    ImGui::SameLine();
    static const int quick[] = {4, 6, 8, 10, 12, 20};
    for (int q : quick) {
        char label[8];
        std::snprintf(label, sizeof label, "D%d", q);
        if (ImGui::Button(label)) rollExpression(label, "quick roll");
        ImGui::SameLine(0, 4);
    }
    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth(120);
    const bool enter = ImGui::InputText("##dice", diceExpr_, sizeof diceExpr_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Roll") || enter) rollExpression(diceExpr_, "free roll");
    ImGui::SameLine(0, 16);
    if (!rolls_.empty()) {
        const bool flash = nowSeconds() < rollFlashUntil_;
        const DiceRoll& r = rolls_.front();
        bigText(std::to_string(r.total).c_str(), 1.5f, flash ? kRed : ImGui::GetStyle().Colors[ImGuiCol_Text]);
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "%s", r.describe().c_str());
        std::string older;
        for (size_t i = 1; i < rolls_.size() && i < 7; ++i) older += (i > 1 ? "   " : "") + rolls_[i].describe();
        if (!older.empty()) {
            ImGui::SameLine(0, 18);
            ImGui::TextColored(kGrey, "%s", older.c_str());
        }
    }
}

// What the right pane shows before anything is selected.
void App::drawWelcome() {
    ImGui::Spacing();
    bigText("Skaldbok", 1.7f, kAccent);
    ImGui::TextWrapped("Pick something from the list, or press Ctrl+K and search creatures, rules, tables and every content pack. "
                       "Every entry links back to the page it came from.");
    ImGui::Spacing();
    for (const PackInfo& p : content_.packs()) {
        if (!p.loaded) continue;
        for (int sid : p.sourceIds) {
            const SourceInfo* s = content_.source(sid);
            if (!s) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, sourceColor(content_, sid));
            ImGui::BulletText("%s", s->title.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (!recents_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kAccent, "Recently viewed");
        size_t shown = 0;
        for (const Recent& r : recents_) {
            const std::string name = nameOf(r);
            const int id = content_.idByKey(r.kind, r.key);
            if (name.empty() || !id || !kindAvailable(r.kind)) continue;
            if (shown++ >= 12) break;
            ImGui::PushID(static_cast<int>(shown));
            if (ImGui::Button((name + "##" + std::string(kindKey(r.kind)) + r.key).c_str())) navigate(r.kind, id, true);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    ImGui::Spacing();
    ImGui::TextColored(kAccent, "Shortcuts");
    ImGui::BulletText("Ctrl+K   search everything");
    ImGui::BulletText("Alt+Left / Alt+Right   back and forward");
    ImGui::BulletText("Tab   switch between the nav rail and the page on the right (in any module)");
    ImGui::BulletText("Up / Down   move through the nav rail or a list; the highlighted one opens immediately");
    ImGui::BulletText("Left / Right   between a page's Intro and its list (Creatures, Spells...)");
    ImGui::BulletText("Ctrl+F   the filter of the page you are on; Enter or Down goes back to the list");
    ImGui::BulletText("Ctrl +  /  Ctrl -  /  Ctrl 0   text size");
    ImGui::Spacing();
    ImGui::TextColored(kGrey, "The gear (top right) and the Web server button open the players' web server page.");
}

void App::frame(float width, float height) {
    if (reloadRequested_) reloadContent();
    ImGuiIO& io = ImGui::GetIO();
    for (const auto& m : modules_)
        m->update();

    if (!active_) showModule("search");

    // Tab belongs to the shell (rail <-> page, below). Owning it keeps ImGui's own nav from also using it to hop between
    // widgets inside a panel; the ownership set now is what next frame's nav update sees, so it is renewed every frame.
    ImGui::SetKeyOwner(ImGuiKey_Tab, ImGui::GetID("##shell-tab"));

    // When the nav rail last saw a movement key: the highlight moving onto a module entry only opens it if an arrow
    // moved it (ImGui applies a move a frame later, hence the margin). A highlight that merely comes back to an entry -
    // the shell handing the keyboard back to the rail after a click elsewhere, say the gear - must not open anything.
    for (ImGuiKey k : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_PageUp, ImGuiKey_PageDown})
        if (ImGui::IsKeyPressed(k)) navArrowFrame_ = ImGui::GetFrameCount();

    // shortcuts
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        focusSearch_ = true;
        showModule("search");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput && active_ && std::string(active_->id()) == "web") toggleServerPage();
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) goBack();
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) goForward();
    if (ImGui::IsMouseClicked(3) && !io.WantTextInput) goBack();                   // the mouse's side buttons
    if (ImGui::IsMouseClicked(4) && !io.WantTextInput) goForward();
    if (io.KeyCtrl) {
        const bool plus = ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd);
        const bool minus = ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract);
        if (plus) zoom_ = std::min(2.0f, zoom_ + 0.1f);
        if (minus) zoom_ = std::max(0.7f, zoom_ - 0.1f);
        if (ImGui::IsKeyPressed(ImGuiKey_0, false)) zoom_ = 1.0f;
        if (plus || minus || ImGui::IsKeyPressed(ImGuiKey_0, false)) ImGui::GetStyle().FontScaleMain = zoom_;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollWithMouse);
    drawTopBar();
    const float diceH = ImGui::GetFrameHeight() + 22.0f;
    const float bodyH = ImGui::GetContentRegionAvail().y - diceH;
    const float navW = U(190.0f);
    const float listW = std::clamp(width * 0.27f, U(280.0f), U(420.0f));

    ImGui::BeginChild("##nav", ImVec2(navW, bodyH));
    drawNav();
    if (active_ && active_ != recorded_) {                 // a page opened from the rail (or the gear) is a place too
        Place p;
        p.module = active_->id();
        p.label = active_->title();
        pushPlace(std::move(p));
    }
    const bool navFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);   // not RootAndChildWindows: the root is "##root", i.e. the whole app
    ImGui::EndChild();
    ImGui::SameLine();
    Module* m = active_;
    bool contentFocused = false;
    // What the page's own keyboard handling reads (ui::pageKeys()): last frame's answer, the focus is only known once
    // the page has been drawn. The keyboard is "in use" from an arrow or Tab until the next mouse click.
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        keyboardInUse_ = true;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) keyboardInUse_ = false;
    setPageKeyboard(pageHadKeyboard_, keyboardInUse_);
    if (m->layout() == Layout::Full) {
        // the whole width: a catalog page (its own list + detail), the settings
        ImGui::BeginChild("##full", ImVec2(0, bodyH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (wantFocusContent_) ImGui::SetWindowFocus();
        m->drawFull();
        contentFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        if (contentFocused) focusFrame();
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##list", ImVec2(listW, bodyH), ImGuiChildFlags_Borders);
        if (wantFocusContent_) ImGui::SetWindowFocus();   // the panel, not a row: a list reads the arrows while its panel is focused
        m->drawList();
        contentFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        if (contentFocused) focusFrame();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##detail", ImVec2(0, bodyH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (m->ownsDetail()) {
            m->drawDetail();
        } else if (Module* h = hasSel_ ? handlerFor(sel_.kind, sel_.id) : nullptr) {
            h->drawSelection(sel_);
        } else {
            drawWelcome();
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
            contentFocused = true;
            focusFrame();
        }
        ImGui::EndChild();
    }
    wantFocusContent_ = false;
    pageHadKeyboard_ = contentFocused;
    // Tab only ever crosses between the nav rail and the page on the right (never moves inside either: that is the
    // arrows' job), in every module and even while typing in a filter. From the top bar or the dice bar it goes to
    // the page. Consumed at the top of the target region next frame.
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        ImGui::SetWindowFocus(nullptr);           // drops a text field that has the keyboard (a filter, the search box)
        if (navFocused) wantFocusContent_ = true;
        else if (contentFocused) wantFocusNav_ = true;
        else wantFocusContent_ = true;
    }
    // The keyboard is never left nowhere: when neither side has it (the start, a click on the dice bar or the
    // background, a popup that closed), it goes back to the side that had it last - the rail at the start, where
    // "Search" is the open module. Not while typing in a text field or with a popup open.
    if (navFocused) keyboardOnRail_ = true;
    else if (contentFocused) keyboardOnRail_ = false;
    else if (!wantFocusNav_ && !wantFocusContent_ && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
             !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (keyboardOnRail_) wantFocusNav_ = true;
        else wantFocusContent_ = true;
    }

    ImGui::BeginChild("##dice", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);          // mouse only, like the top bar: keyboard focus stays on rail / page
    drawDiceBar();
    ImGui::PopItemFlag();
    ImGui::EndChild();
    ImGui::End();

    if (nowSeconds() < toastUntil_ && !toast_.empty()) {
        ImGui::SetNextWindowPos(ImVec2(20, height - diceH - 20), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::Begin("##toast", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(toast_.c_str());
        ImGui::End();
    }
}

}  // namespace gm
