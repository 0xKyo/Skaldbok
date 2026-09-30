#include "ui/app.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

double nowSeconds() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }

}  // namespace

// ------------------------------------------------------------------------------------- setup

App::App(SDL_Window* window, SDL_Renderer* renderer, Paths paths) : window_(window), renderer_(renderer), paths_(std::move(paths)) {
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
        settings_.open(prefDir + "settings.yaml");
    }
    packs_ = std::make_unique<PackManager>(paths_.coreDir(), paths_.userPacksDir());

    setupStyle();
    loadContent();
    screen_ = std::make_unique<SettingsScreen>(*this);
}

App::~App() { screen_.reset(); }                           // (stops the web server before the rest of the members go)

void App::loadContent() {
    const std::vector<PackSpec> specs = packs_->specs(settings_.disabledPacks());
    packSig_ = packSignature(specs);                       // taken before reading: a file that changes meanwhile is noticed next time
    pendingSig_.clear();
    content_.load(specs);
}

// Packs were imported, removed or switched: read everything again.
void App::reloadContent() {
    reloadRequested_ = false;
    const bool fromDisk = std::exchange(reloadFromDisk_, false);
    loadContent();
    if (fromDisk) {
        for (const PackInfo& p : content_.packs())
            if (!p.error.empty()) {
                notify("Pack " + (p.name.empty() ? p.dir : p.name) + ": " + p.error.substr(0, 140));
                return;
            }
        notify("Content reloaded");
    }
}

void App::notify(const std::string& text) {
    toast_ = text;
    toastUntil_ = nowSeconds() + 3.5;
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
    return any;
}

bool App::wantsRedraw() const { return nowSeconds() < toastUntil_ || reloadRequested_ || (screen_ && screen_->wantsRedraw()); }

void App::applyStartup(const StartupOptions& o) {
    if (o.importPath.empty()) return;
    const ImportResult r = packs_->import(o.importPath);
    std::printf("%s\n", r.message.c_str());
    for (const std::string& w : r.warnings) std::printf("  warning: %s\n", w.c_str());
    if (r.ok) settings_.setPackEnabled(r.packId, true);
    reloadContent();
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
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccentDim;
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.878f, 0.835f, 0.722f, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.725f, 0.659f, 0.518f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.627f, 0.561f, 0.408f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.545f, 0.475f, 0.329f, 1.0f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.184f, 0.478f, 0.408f, 0.30f);
    c[ImGuiCol_NavCursor] = kAccentDim;
}

// ------------------------------------------------------------------------------------ frame

void App::frame(float width, float height) {
    if (reloadRequested_) reloadContent();
    ImGuiIO& io = ImGui::GetIO();
    screen_->update();

    if (io.KeyCtrl) {                                                                  // text size
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
    screen_->draw();
    ImGui::End();

    if (nowSeconds() < toastUntil_ && !toast_.empty()) {
        ImGui::SetNextWindowPos(ImVec2(20, height - 20), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::Begin("##toast", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(toast_.c_str());
        ImGui::End();
    }
}

}  // namespace gm
