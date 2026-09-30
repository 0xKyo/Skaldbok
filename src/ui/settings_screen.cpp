#include "ui/settings_screen.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <imgui.h>

#include "parsing/fsutil.h"
#include "parsing/jsonutil.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

#ifdef _WIN32
constexpr const char* kServerName = "skaldbok_web.exe";
#else
constexpr const char* kServerName = "skaldbok_web";
#endif

}  // namespace

SettingsScreen::SettingsScreen(Host& host) : host_(host), packs_(host) {
    std::snprintf(port_, sizeof port_, "%d", host_.settings().web.port);
    std::snprintf(publicUrl_, sizeof publicUrl_, "%s", host_.settings().web.publicUrl.c_str());
    // off until the GM starts it, unless "Launch server when opening" is ticked (and never with --no-web, for tests)
    const char* off = SDL_getenv("SKALDBOK_NO_WEB");
    if (host_.settings().web.launchOnOpen && !(off && *off == '1')) start();
}

SettingsScreen::~SettingsScreen() { stop(); }

bool SettingsScreen::wantsRedraw() const { return packs_.busy() || (proc_ && SDL_GetTicks() < watchUntil_); }   // while it is starting, keep looking

void SettingsScreen::update() {
    packs_.update();
    if (!proc_) return;
    int code = 0;
    if (SDL_WaitProcess(proc_, false, &code)) {                    // it ended by itself: read why
        SDL_DestroyProcess(proc_);
        proc_ = nullptr;
        readStatus();
        message_ = status_.error.empty() ? "The web server stopped." : status_.error;
    } else if (SDL_GetTicks() >= nextRead_) {
        readStatus();
        nextRead_ = SDL_GetTicks() + (status_.running ? 1000 : 300);
        if (status_.running) watchUntil_ = 0;                        // it is up: nothing to wait for any more
    }
}

void SettingsScreen::draw() {
    WebSettings& w = host_.settings().web;
    ImGui::BeginChild("##settings", ImVec2(0, 0));

    // ---- general settings: the content packs -----------------------------------------------------------
    bigText("General Settings", 1.6f, kAccent);
    bigText("Content packs", 1.15f, kGold);
    packs_.draw();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- the web server ----------------------------------------------------------------------------------
    bigText("Web server for players", 1.6f, kAccent);
    ImGui::TextWrapped("Each player opens their own link in a browser (a phone is fine) and sees their character and the rules. "
                       "You open the GM page with the button below. The page updates by itself when anything changes. The server is off until you start it below.");

    // ---- state ---------------------------------------------------------------------------------------------
    ImGui::Spacing();
    const bool running = proc_ && status_.running;
    if (running) {
        ImGui::TextColored(kAccent, "Running");
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "on this computer at %s", status_.localUrl.c_str());
        ImGui::TextColored(kGold, "Players on your network:");
        ImGui::SameLine();
        ImGui::TextUnformatted(status_.url.c_str());
    } else if (proc_) {
        ImGui::TextColored(kGold, "Starting…");
    } else {
        ImGui::TextColored(message_.empty() ? kGrey : kRed, "%s", message_.empty() ? "Stopped" : message_.c_str());
    }

    if (!proc_) {
        if (ImGui::Button("Start server")) start();
    } else {
        if (ImGui::Button("Stop")) {
            stop();
            message_ = "Stopped";
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart")) {
            stop();
            start();
        }
    }
    if (running) {
        ImGui::SameLine();
        if (ImGui::Button("Open in this browser")) SDL_OpenURL(status_.localUrl.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(status_.gmLink.empty());
        if (ImGui::Button("Open as GM")) SDL_OpenURL(status_.gmLink.c_str());     // the link carries the GM token
        ImGui::EndDisabled();
    }

    // ---- options -----------------------------------------------------------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Checkbox("Launch server when opening", &w.launchOnOpen)) host_.settings().save();
    ImGui::SetNextItemWidth(U(90));
    ImGui::InputText("Port", port_, sizeof port_, ImGuiInputTextFlags_CharsDecimal);
    ImGui::SetNextItemWidth(U(360));
    ImGui::InputTextWithHint("Address players use", "automatic: this computer's address on the network", publicUrl_, sizeof publicUrl_);
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        w.port = std::clamp(std::atoi(port_), 1, 65535);
        w.publicUrl = publicUrl_;
        std::snprintf(port_, sizeof port_, "%d", w.port);
        host_.settings().save();
        if (proc_) {
            stop();
            start();
        }
    }
    ImGui::TextColored(kGrey, "For players outside your home network, put the https address of a tunnel here (see docs/WEB.md).");

    ImGui::Spacing();
    ImGui::TextWrapped("The first time, Windows may ask whether to let \"skaldbok_web\" use the network: allow it on private networks so "
                       "phones on your Wi-Fi can connect. The players' links, and the rest, are on the web page (Open as GM).");
    ImGui::EndChild();
}

std::string SettingsScreen::statusFile() const { return host_.paths().prefDir + "web-status.yaml"; }

void SettingsScreen::readStatus() {
    Status s;
    if (const auto j = jsonLoad(statusFile())) {
        s.running = jsonBool(*j, "running");
        s.url = jsonStr(*j, "url");
        s.localUrl = jsonStr(*j, "local_url");
        s.gmLink = jsonStr(*j, "gm_link");
        s.error = jsonStr(*j, "error");
    }
    status_ = s;
}

void SettingsScreen::start() {
    if (proc_) return;
    std::string dir = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    const std::string exe = dir + kServerName;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(exe.c_str(), &info)) {
        message_ = std::string("The web server program (") + kServerName + ") was not found next to the app.";
        return;
    }
    const std::string prefs = fs::withoutTrailingSlash(host_.paths().prefDir);
    const WebSettings& w = host_.settings().web;
#ifdef _WIN32
    const long long me = static_cast<long long>(GetCurrentProcessId());
#else
    const long long me = static_cast<long long>(getpid());
#endif
    // --parent: the server ends by itself if this app disappears (killed, crashed), so it never lingers holding the port
    std::vector<std::string> args = {exe, "--prefs", prefs, "--data", host_.paths().dataDir, "--port", std::to_string(w.port), "--parent", std::to_string(me)};
    if (!w.publicUrl.empty()) {
        args.push_back("--public-url");
        args.push_back(w.publicUrl);
        // an https address means a tunnel or proxy in front: every request then arrives from it, so failed attempts
        // must be counted per real client (X-Forwarded-For), not all against the tunnel's address
        if (w.publicUrl.starts_with("https://")) args.push_back("--trust-proxy");
    }
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_RemovePath(statusFile().c_str());                        // a leftover from an earlier run must not pass for this one
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);      // no console window
    proc_ = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    status_ = Status{};
    message_.clear();
    if (!proc_) message_ = std::string("Could not start the web server: ") + SDL_GetError();
    watchUntil_ = SDL_GetTicks() + 15000;                        // keep the screen refreshing until the server says it is up
    nextRead_ = 0;
}

void SettingsScreen::stop() {
    if (!proc_) return;
    SDL_KillProcess(proc_, true);
    SDL_DestroyProcess(proc_);
    proc_ = nullptr;
    SDL_RemovePath(statusFile().c_str());
    status_ = Status{};
}

}  // namespace gm
