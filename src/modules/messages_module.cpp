// Messages: the chat with each player, and broadcasts to a party or to everyone. Players write from their page of the web view; what
// they send shows up here as it happens (the app looks at the chat folder a few times a second). Players only ever talk to the GM.
#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

#include <imgui.h>

#include "filedialog.h"
#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

constexpr ImU32 kGmBubble = IM_COL32(47, 122, 104, 255), kGmText = IM_COL32(243, 234, 210, 255);
constexpr ImU32 kPlayerBubble = IM_COL32(250, 243, 222, 255), kPlayerEdge = IM_COL32(185, 168, 132, 255);
constexpr ImU32 kBroadcastFill = IM_COL32(236, 218, 160, 255), kBroadcastEdge = IM_COL32(176, 35, 27, 255);

std::string clock(const std::string& iso) { return iso.size() >= 16 ? iso.substr(11, 5) : std::string(); }
std::string day(const std::string& iso) { return iso.size() >= 10 ? iso.substr(0, 10) : std::string(); }

class MessagesModule : public Module, public IMessenger {
public:
    using Module::Module;

    const char* id() const override { return "messages"; }
    const char* title() const override { return "Messages"; }
    const char* summary() const override { return "Chat with each player, and broadcast to a party or to everyone; players write from their page of the web view."; }
    const char* group() const override { return "Play"; }
    Layout layout() const override { return Layout::Full; }
    bool wantsRedraw() const override { return dialog_.busy(); }

    int badge() const override {
        int total = 0;
        for (const Character& c : host_.characters().all()) total += host_.messages().unread(c.id, true);
        return total > 0 ? total : -1;
    }

    void compose(const std::string& recipient, const std::string& imagePath, const std::string& text) override {
        if (recipient.starts_with("char:")) {
            selected_ = recipient.substr(5);
            draft_ = text;
            image_ = imagePath;
        } else if (!recipient.empty()) {                              // a party or everyone: that is a broadcast
            broadcastTarget_ = recipient;
            broadcastText_ = text;
            broadcastImage_ = imagePath;
            openBroadcast_ = true;
        } else if (!imagePath.empty() || !text.empty()) {
            draft_ = text;
            image_ = imagePath;
        }
        host_.showModule("messages");
    }

    void update() override {
        std::string path;
        if (dialog_.poll(path) && !path.empty()) (pickingBroadcast_ ? broadcastImage_ : image_) = path;
    }

    void drawFull() override {
        if (!host_.characters().find(selected_)) selected_.clear();
        ImGui::BeginChild("##threads", ImVec2(U(250), 0), ImGuiChildFlags_Borders);
        drawThreads();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##conversation", ImVec2(0, 0));
        drawConversation();
        ImGui::EndChild();
        drawBroadcast();
    }

private:
    // ------------------------------------------------------------------------------------------ the list
    struct Row {
        const Character* character;
        std::string last;            // id of the newest message, to order by activity
        std::string snippet;
        int unread;
    };

    void drawThreads() {
        if (ImGui::Button("Broadcast…", ImVec2(-FLT_MIN, 0))) {
            openBroadcast_ = true;
            if (broadcastTarget_.empty()) broadcastTarget_ = "all";
        }
        ImGui::Spacing();
        std::vector<Row> rows;
        for (const Character& c : host_.characters().all()) {
            const Thread& t = host_.messages().thread(c.id);
            Row r{&c, t.messages.empty() ? std::string() : t.messages.back().id, {}, host_.messages().unread(c.id, true)};
            if (!t.messages.empty()) {
                const Message& m = t.messages.back();
                r.snippet = (m.from == "gm" ? "You: " : "") + (m.text.empty() ? std::string("[picture]") : m.text.substr(0, m.text.find('\n')));
            }
            rows.push_back(std::move(r));
        }
        std::ranges::stable_sort(rows, [](const Row& a, const Row& b) { return a.last > b.last; });          // the busiest first
        if (rows.empty()) ImGui::TextWrapped("There are no characters yet. Create them in the Characters module.");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const Row& r : rows) {
            ImGui::PushID(r.character->id.c_str());
            const float h = lineH() * 2.0f + U(10);
            if (ImGui::Selectable("##row", r.character->id == selected_, 0, ImVec2(0, h))) selected_ = r.character->id;
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            dl->PushClipRect(mn, ImVec2(mx.x - U(30), mx.y), true);
            dl->AddText(ImVec2(mn.x + U(8), mn.y + U(5)), ImGui::GetColorU32(ImGuiCol_Text), r.character->name.c_str());
            dl->AddText(ImVec2(mn.x + U(8), mn.y + U(5) + lineH() + U(1)), ImGui::GetColorU32(kGrey), r.snippet.c_str());
            dl->PopClipRect();
            if (r.unread > 0) {
                char n[8];
                std::snprintf(n, sizeof n, "%d", r.unread);
                const ImVec2 c(mx.x - U(16), mn.y + U(14));
                dl->AddCircleFilled(c, U(10), ImGui::GetColorU32(kRed));
                const ImVec2 ts = ImGui::CalcTextSize(n);
                dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, n);
            }
            ImGui::PopID();
        }
    }

    // ------------------------------------------------------------------------------------ a conversation
    void drawConversation() {
        const Character* c = host_.characters().find(selected_);
        if (!c) {
            ImGui::Spacing();
            bigText("Messages", 1.5f, kAccent);
            ImGui::TextWrapped("Pick a player to talk to. What they write from their page appears here by itself. Use Broadcast to send the same "
                               "note to a whole party, marked as a broadcast.");
            return;
        }
        // Reading the conversation is reading it: the player sees it as seen. (Marking refreshes the store's own copy, so the thread
        // is taken afterwards, and as a copy.)
        if (host_.messages().unread(c->id, true) > 0) {
            std::string newest;
            for (const Message& m : host_.messages().thread(c->id).messages)
                if (m.from == "player") newest = m.id;
            host_.messages().markRead(c->id, true, newest);
        }
        const Thread t = host_.messages().thread(c->id);
        bigText(c->displayName().c_str(), 1.3f, ImGui::GetStyle().Colors[ImGuiCol_Text]);
        if (!c->player.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "played by %s", c->player.c_str());
        }
        ImGui::Separator();

        const float inputH = ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeightWithSpacing() + U(30);
        ImGui::BeginChild("##messages", ImVec2(0, -inputH));
        std::string lastDay;
        for (const Message& m : t.messages) {
            if (day(m.at) != lastDay) {
                lastDay = day(m.at);
                ImGui::Spacing();
                ImGui::TextColored(kGrey, "%s", lastDay.c_str());
            }
            ImGui::PushID(m.id.c_str());
            if (m.kind == "broadcast") broadcastCard(m);
            else bubble(m, t);
            ImGui::PopID();
        }
        if (t.messages.empty()) ImGui::TextColored(kGrey, "Nothing yet. Say hello.");
        if (scrolledFor_ != c->id + (t.messages.empty() ? "" : t.messages.back().id)) {           // a new message: show it
            scrolledFor_ = c->id + (t.messages.empty() ? "" : t.messages.back().id);
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        drawInput(*c);
    }

    void picture(const Message& m, float maxW) {
        if (m.image.empty()) return;
        if (const TextureCache::Tex* tex = host_.textures().get(host_.messages().mediaFile(m.image))) {
            const float w = std::min(maxW, U(280));
            ImGui::Image(reinterpret_cast<ImTextureID>(tex->tex), ImVec2(w, w * static_cast<float>(tex->h) / static_cast<float>(tex->w)));
        }
    }

    void bubble(const Message& m, const Thread& t) {
        const bool mine = m.from == "gm";
        const float avail = ImGui::GetContentRegionAvail().x, maxW = avail * 0.72f, pad = U(9);
        const float textW = m.text.empty() ? 0.0f : std::min(ImGui::CalcTextSize(m.text.c_str(), nullptr, false, maxW - 2 * pad).x, maxW - 2 * pad);
        const float w = std::max({textW, U(90), m.image.empty() ? 0.0f : U(200)}) + 2 * pad;
        if (mine) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w - ImGui::GetStyle().ScrollbarSize);
        Backdrop b;
        b.begin(w, mine ? kGmBubble : kPlayerBubble, mine ? kGmBubble : kPlayerEdge, pad, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, mine ? ImGui::ColorConvertU32ToFloat4(kGmText) : ImGui::GetStyle().Colors[ImGuiCol_Text]);
        picture(m, w - 2 * pad);
        if (!m.text.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 2 * pad);
            ImGui::TextUnformatted(m.text.c_str());
            ImGui::PopTextWrapPos();
        }
        std::string meta = clock(m.at);
        if (mine && m.id <= t.playerRead) meta += "  seen";
        ImGui::PushStyleColor(ImGuiCol_Text, mine ? ImVec4(0.80f, 0.88f, 0.84f, 1) : kGrey);
        ImGui::TextUnformatted(meta.c_str());
        ImGui::PopStyleColor(2);
        b.end();
    }

    // A broadcast has its own look, the same the player sees: a parchment card with a red edge and a heading.
    void broadcastCard(const Message& m) {
        const float w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
        Backdrop b;
        b.begin(w, kBroadcastFill, kBroadcastEdge, U(10), 8.0f);
        ImGui::TextColored(kRed, "BROADCAST");
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "to %s · %s", m.to.empty() ? "everyone" : m.to.c_str(), clock(m.at).c_str());
        picture(m, w - U(20));
        if (!m.text.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - U(20));
            ImGui::TextUnformatted(m.text.c_str());
            ImGui::PopTextWrapPos();
        }
        b.end();
    }

    void drawInput(const Character& c) {
        ImGui::Separator();
        inputMultiline("##draft", draft_, ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 3.0f + U(8)));
        const bool ctrlEnter = ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        if (draft_.empty() && !ImGui::IsItemActive()) {
            const ImVec2 p = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + U(8), p.y + U(6)), ImGui::GetColorU32(kGrey), "Write to this player…  (Ctrl+Enter sends)");
        }
        const bool ready = !draft_.empty() || !image_.empty();
        ImGui::BeginDisabled(!ready);
        const bool clicked = ImGui::Button("Send");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Picture…")) {
            pickingBroadcast_ = false;
            dialog_.openFile(host_.window(), "Pictures", "png;jpg;jpeg;gif;webp");
        }
        ImGui::SameLine();
        if (ImGui::Button("Creature art…")) {
            pickingBroadcast_ = false;
            ImGui::OpenPopup("##creatureart");
        }
        if (!image_.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Remove picture")) image_.clear();
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "%s", image_.c_str());
        }
        pickCreatureArt();
        if (clicked || (ctrlEnter && ready)) {
            while (draft_.ends_with('\n')) draft_.pop_back();               // Ctrl+Enter also typed a new line
            std::string err;
            const std::vector<std::string> to = {c.id};
            if (host_.messages().sendFromGm(to, false, "", draft_, image_, &err)) {
                draft_.clear();
                image_.clear();
                scrolledFor_.clear();
            } else {
                host_.notify("Could not send: " + err);
            }
        }
    }

    // ---------------------------------------------------------------------------------------- broadcast
    void drawBroadcast() {
        if (openBroadcast_) {
            ImGui::OpenPopup("Broadcast");
            openBroadcast_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(U(520), 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Broadcast", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped("The same note for a whole party, marked as a broadcast in each player's chat. Their replies come back to you, one to one.");
        std::vector<std::pair<std::string, std::string>> options = {{"all", "Everyone"}};
        for (const Party& p : host_.parties().all()) options.push_back({"party:" + p.id, "Party: " + p.name});
        const auto chosen = std::ranges::find(options, broadcastTarget_, &std::pair<std::string, std::string>::first);
        if (chosen == options.end()) broadcastTarget_ = "all";
        ImGui::SetNextItemWidth(U(320));
        if (ImGui::BeginCombo("To", (chosen == options.end() ? options.front() : *chosen).second.c_str())) {
            for (const auto& [key, label] : options)
                if (ImGui::Selectable(label.c_str(), broadcastTarget_ == key)) broadcastTarget_ = key;
            ImGui::EndCombo();
        }
        inputMultiline("##bctext", broadcastText_, ImVec2(-FLT_MIN, U(110)));
        if (ImGui::Button("Picture…")) {
            pickingBroadcast_ = true;
            dialog_.openFile(host_.window(), "Pictures", "png;jpg;jpeg;gif;webp");
        }
        ImGui::SameLine();
        if (ImGui::Button("Creature art…")) {
            pickingBroadcast_ = true;
            ImGui::OpenPopup("##creatureart");
        }
        if (!broadcastImage_.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Remove picture")) broadcastImage_.clear();
            ImGui::TextColored(kGrey, "%s", broadcastImage_.c_str());
        }
        pickCreatureArt();

        std::vector<std::string> ids;
        std::string label = "Everyone";
        if (broadcastTarget_ == "all") {
            for (const Character& c : host_.characters().all()) ids.push_back(c.id);
        } else if (const Party* p = host_.parties().find(broadcastTarget_.substr(std::min<size_t>(6, broadcastTarget_.size())))) {
            ids = p->members;
            label = p->name;
        }
        std::erase_if(ids, [&](const std::string& id) { return !host_.characters().find(id); });
        const bool ready = !ids.empty() && (!broadcastText_.empty() || !broadcastImage_.empty());
        ImGui::BeginDisabled(!ready);
        char text[48];
        std::snprintf(text, sizeof text, "Send to %d player%s", static_cast<int>(ids.size()), ids.size() == 1 ? "" : "s");
        if (ImGui::Button(text)) {
            std::string err;
            if (host_.messages().sendFromGm(ids, true, label, broadcastText_, broadcastImage_, &err)) {
                host_.notify("Broadcast sent to " + label);
                broadcastText_.clear();
                broadcastImage_.clear();
                ImGui::CloseCurrentPopup();
            } else {
                host_.notify("Could not send: " + err);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void pickCreatureArt() {
        ImGui::SetNextWindowSize(ImVec2(U(380), U(320)), ImGuiCond_Appearing);
        if (!ImGui::BeginPopup("##creatureart")) return;
        if (ImGui::IsWindowAppearing()) {
            artFilter_[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##artfilter", "Filter creatures…", artFilter_, sizeof artFilter_);
        const std::string needle = lowered(artFilter_);
        ImGui::BeginChild("##artlist", ImVec2(0, 0));
        for (const Monster& m : host_.content().monsters()) {
            if (m.image.empty() || (!needle.empty() && !lowered(m.name).contains(needle))) continue;
            const SourceInfo* si = host_.content().source(m.sourceId);
            ImGui::PushID(m.key.c_str());
            if (ImGui::Selectable((m.name + "  (" + (si ? si->label : std::string("?")) + ")").c_str())) {
                (pickingBroadcast_ ? broadcastImage_ : image_) = m.image;
                std::string& text = pickingBroadcast_ ? broadcastText_ : draft_;
                if (text.empty()) text = m.name;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    FileDialog dialog_;
    std::string selected_;                                 // the character whose conversation is open
    std::string draft_, image_;
    std::string broadcastTarget_ = "all", broadcastText_, broadcastImage_;
    bool openBroadcast_ = false;
    bool pickingBroadcast_ = false;                        // which of the two forms the file dialog / creature list feeds
    std::string scrolledFor_;
    char artFilter_[64] = {};
};

}  // namespace

std::unique_ptr<Module> makeMessagesModule(Host& host) { return std::make_unique<MessagesModule>(host); }

}  // namespace gm
