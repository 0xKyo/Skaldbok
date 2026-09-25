// Master Screen: an infinite canvas of pinned references for the GM (creatures, spells, tables, characters, the
// party, notes, images). Boards, positions and sizes are saved per user (master_screen.json); references follow the live content.
//
// Coordinates: items live in "canvas units" (16 px at zoom 1 on a 100% display). screen = origin + (view + pos * zoom) * ui,
// where ui is the display/user font scale. Inside an item the font and the style are scaled by `zoom`, so the ui:: helpers and
// U() draw at the right size without knowing about the canvas.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "ui/filedialog.h"
#include "game/master_screen.h"
#include "ui/modules/modules.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

constexpr float kTitleH = 26.0f;        // title bar height, canvas units
constexpr float kGripH = 12.0f;         // resize strip under the body
constexpr float kLodZoom = 0.5f;        // below this the items are plain labelled blocks
constexpr size_t kUndoLimit = 20;

const ImVec4 kPalette[12] = {{0.86f, 0.33f, 0.30f, 1}, {0.90f, 0.55f, 0.24f, 1}, {0.88f, 0.75f, 0.28f, 1}, {0.60f, 0.78f, 0.30f, 1},
                             {0.28f, 0.72f, 0.45f, 1}, {0.26f, 0.72f, 0.72f, 1}, {0.30f, 0.60f, 0.90f, 1}, {0.42f, 0.45f, 0.90f, 1},
                             {0.62f, 0.42f, 0.88f, 1}, {0.82f, 0.42f, 0.78f, 1}, {0.88f, 0.42f, 0.55f, 1}, {0.60f, 0.62f, 0.66f, 1}};

ImVec4 typeColor(const std::string& type) {
    if (type == "note") return {0.88f, 0.75f, 0.28f, 1};
    if (type == "creature") return {0.86f, 0.33f, 0.30f, 1};
    if (type == "table") return {0.28f, 0.72f, 0.45f, 1};
    if (type == "character") return {0.30f, 0.60f, 0.90f, 1};
    if (type == "party") return {0.90f, 0.55f, 0.24f, 1};
    if (type == "image") return {0.60f, 0.62f, 0.66f, 1};
    return {0.26f, 0.72f, 0.72f, 1};     // entry
}

ImVec4 itemColor(const ScreenItem& it) { return it.color > 0 ? kPalette[it.color - 1] : typeColor(it.type); }
ImU32 u32(ImVec4 c, float mul = 1.0f, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32({c.x * mul, c.y * mul, c.z * mul, alpha}); }

std::string stemOf(std::string path) {
    for (char& c : path)
        if (c == '\\') c = '/';
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) path.erase(0, slash + 1);
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && dot > 0) path.erase(dot);
    return path;
}

class MasterScreenModule : public Module, public IMasterScreen {
public:
    explicit MasterScreenModule(Host& host) : Module(host) {
        const std::string path = host_.paths().masterScreenFile();
        SDL_PathInfo info;
        if (!data_.load(path) && SDL_GetPathInfo(path.c_str(), &info)) SDL_RenamePath(path.c_str(), (path + ".damaged").c_str());   // keep an unreadable file
        data_.ensureBoard();
    }
    ~MasterScreenModule() override { save(); }

    const char* id() const override { return "screen"; }
    const char* title() const override { return "Master Screen"; }
    const char* group() const override { return "Play"; }
    Layout layout() const override { return Layout::Full; }
    bool wantsRedraw() const override { return dirty_ || dialog_.busy(); }

    void onContentChanged() override {
        tables_.clear();
    }

    void update() override {
        if (dirty_ && SDL_GetTicks() >= saveAt_) save();
        std::string path;
        if (dialog_.poll(path) && !path.empty()) pinImage(path);
    }

    // ------------------------------------------------------------------------------------------ IMasterScreen
    void pinEntry(Kind kind, int id) override {
        ScreenItem it;
        it.ref = host_.content().keyOf(kind, id);
        if (it.ref.empty()) return;
        it.kind = kindKey(kind);
        it.title = host_.content().titleOf(kind, id);
        switch (kind) {
            case Kind::Monster: it.type = "creature"; it.w = 440; it.h = 560; break;
            case Kind::Table: it.type = "table"; it.w = 420; it.h = 340; break;
            default: it.type = "entry"; it.w = 340; it.h = 320; break;
        }
        pin(std::move(it));
    }

    void pinCharacter(const std::string& characterId) override {
        const Character* c = host_.characters().find(characterId);
        if (!c) return;
        ScreenItem it;
        it.type = "character";
        it.ref = characterId;
        it.title = c->displayName();
        it.w = 340;
        it.h = 420;
        pin(std::move(it));
    }

    void pinParty(const std::string& partyId) override {
        const Party* p = host_.parties().find(partyId);
        if (!p) return;
        ScreenItem it;
        it.type = "party";
        it.ref = partyId;
        it.title = p->name;
        it.w = 340;
        it.h = 360;
        pin(std::move(it));
    }

    void pinImage(const std::string& path, float windowX = -1, float windowY = -1) override {
        const TextureCache::Tex* t = host_.textures().get(path);
        if (!t || t->w <= 0) {
            host_.notify("That file is not an image the app can open.");
            return;
        }
        ScreenItem it;
        it.type = "image";
        it.image = path;
        for (char& c : it.image)
            if (c == '\\') c = '/';
        it.title = stemOf(path);
        it.w = 360;
        it.h = std::clamp(340.0f * static_cast<float>(t->h) / static_cast<float>(t->w), 80.0f, 700.0f) + kTitleH + kGripH + 16;
        const bool dropped = windowX >= 0 && frameSeen_ + 2 >= ImGui::GetFrameCount() && windowX >= origin_.x && windowY >= origin_.y &&
                             windowX <= origin_.x + canvasPx_.x && windowY <= origin_.y + canvasPx_.y;
        if (dropped) {
            const Board& b = *data_.active();
            it.x = ((windowX - origin_.x) / ui_ - b.viewX) / b.zoom - it.w / 2;
            it.y = ((windowY - origin_.y) / ui_ - b.viewY) / b.zoom - it.h / 2;
            addAt(std::move(it));
        } else {
            pin(std::move(it));
        }
    }

    void demoBoard() override {
        Board& b = data_.addBoard("Session notes");
        auto add = [&](ScreenItem it, float x, float y) {
            it.x = x;
            it.y = y;
            return data_.addItem(b, std::move(it)).id;
        };
        ScreenItem note;
        note.type = "note";
        note.title = "Tonight";
        note.text = "The party reaches the ruined watchtower at dusk.\n\n- Wolves in the yard (2 x Wolf)\n- Locked door: Picking Locks -2\n- The captain lies about the map\n\nIf they linger: random encounter, D6.";
        note.w = 300;
        note.h = 230;
        note.color = 3;
        add(note, 0, 0);
        int shown = 0;
        for (const Monster& m : host_.content().monsters()) {
            if (m.blocks.empty() || m.image.empty() || shown >= 2) continue;
            ScreenItem it;
            it.type = "creature";
            it.kind = "monster";
            it.ref = m.key;
            it.title = m.name;
            it.w = 460;
            it.h = 560;
            add(it, 330.0f + static_cast<float>(shown) * 490.0f, -20);
            ++shown;
        }
        if (const DataTable* t = host_.content().tableByRole("weakness")) {
            ScreenItem it;
            it.type = "table";
            it.kind = "table";
            it.ref = t->key;
            it.title = t->title;
            it.w = 420;
            it.h = 300;
            add(it, 0, 270);
        }
        for (Kind k : {Kind::Spell, Kind::Ability}) {
            const auto& list = host_.content().entries(k);
            if (list.empty()) continue;
            ScreenItem it;
            it.type = "entry";
            it.kind = kindKey(k);
            it.ref = list.front().key;
            it.title = list.front().title;
            it.w = 340;
            it.h = 300;
            add(it, k == Kind::Spell ? 330.0f : 700.0f, 590);
        }
        if (!host_.characters().all().empty()) {
            ScreenItem it;
            it.type = "character";
            it.ref = host_.characters().all().front().id;
            it.w = 340;
            it.h = 420;
            add(it, 1050, 590);
        }
        fitPending_ = true;
        tabFrames_ = 2;
        markDirty();
    }

    // ------------------------------------------------------------------------------------------------- UI
    void drawFull() override {
        frameSeen_ = ImGui::GetFrameCount();
        ui_ = ImGui::GetFontSize() / 16.0f;
        data_.ensureBoard();
        drawTabs();
        Board& b = *data_.active();
        drawToolbar(b);
        drawCanvas(b);
        drawPopups(b);
        applyPending();
    }

private:
    struct Removed {
        std::string boardId;
        ScreenItem item;
    };
    struct TableView {
        DataTable table;
        bool ok = false;
        int row = -1, roll = 0;
    };

    // --------------------------------------------------------------------------------------------- saving
    void markDirty() {
        dirty_ = true;
        saveAt_ = SDL_GetTicks() + 800;
    }

    void save() {
        if (!dirty_) return;
        std::string err;
        if (data_.save(host_.paths().masterScreenFile(), &err)) {
            dirty_ = false;
        } else {
            host_.notify("Could not save the Master Screen: " + err);
            saveAt_ = SDL_GetTicks() + 10000;
        }
    }

    // ---------------------------------------------------------------------------------------------- adding
    ImVec2 viewCenter(const Board& b) const { return {(canvasUnits_.x * 0.5f - b.viewX) / b.zoom, (canvasUnits_.y * 0.5f - b.viewY) / b.zoom}; }

    void addAt(ScreenItem it) {
        Board& b = *data_.active();
        selected_ = data_.addItem(b, std::move(it)).id;
        markDirty();
    }

    // Puts a new item in the middle of the view, staggered so a series of pins does not pile up in one spot.
    void place(ScreenItem it) {
        const Board& b = *data_.active();
        const ImVec2 c = viewCenter(b);
        const float stagger = static_cast<float>(b.items.size() % 8) * 28.0f;
        it.x = c.x - it.w / 2 + stagger;
        it.y = c.y - it.h / 2 + stagger;
        addAt(std::move(it));
    }

    void pin(ScreenItem it) {
        Board& b = *data_.active();
        for (const ScreenItem& o : b.items)
            if (o.type == it.type && o.kind == it.kind && o.ref == it.ref && !it.ref.empty()) {
                selected_ = o.id;
                data_.bringToFront(b, o.id);
                focus(b, o);
                markDirty();
                host_.notify("Already on the Master Screen; brought to the front.");
                return;
            }
        place(std::move(it));
        host_.notify("Pinned to the Master Screen.");
    }

    void addNote(float x, float y, bool atPoint) {
        ScreenItem n;
        n.type = "note";
        n.title = "Note";
        n.w = 260;
        n.h = 200;
        n.color = 3;
        if (atPoint) {
            n.x = x;
            n.y = y;
            addAt(std::move(n));
        } else {
            place(std::move(n));
        }
        renaming_.clear();
    }

    void focus(Board& b, const ScreenItem& it) {
        b.viewX = canvasUnits_.x * 0.5f - (it.x + it.w * 0.5f) * b.zoom;
        b.viewY = canvasUnits_.y * 0.5f - (it.y + std::min(it.h, 300.0f) * 0.5f) * b.zoom;
    }

    void zoomAbout(Board& b, float newZoom, ImVec2 anchor) {
        newZoom = MasterScreenData::clampZoom(newZoom);
        b.viewX = anchor.x - (anchor.x - b.viewX) * newZoom / b.zoom;
        b.viewY = anchor.y - (anchor.y - b.viewY) * newZoom / b.zoom;
        b.zoom = newZoom;
        markDirty();
    }

    // ----------------------------------------------------------------------------------------- tabs & bar
    void drawTabs() {
        std::string picked;
        if (ImGui::BeginTabBar("##boards", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (Board& bd : data_.boards) {
                const ImGuiTabItemFlags flags = tabFrames_ > 0 && bd.id == data_.activeId ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem((bd.name + "###" + bd.id).c_str(), nullptr, flags)) {
                    picked = bd.id;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Rename…")) {
                        boardAction_ = BoardAction::Rename;
                        boardTarget_ = bd.id;
                        std::snprintf(nameBuf_, sizeof nameBuf_, "%s", bd.name.c_str());
                    }
                    if (ImGui::MenuItem("Delete board…", nullptr, false, data_.boards.size() > 1)) {
                        boardAction_ = BoardAction::Delete;
                        boardTarget_ = bd.id;
                    }
                    ImGui::EndPopup();
                }
            }
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) boardAction_ = BoardAction::Add;
            ImGui::EndTabBar();
        }
        if (tabFrames_ > 0) --tabFrames_;
        else if (!picked.empty() && picked != data_.activeId) {
            data_.activeId = picked;
            selected_.clear();
            renaming_.clear();
            markDirty();
        }
    }

    void drawToolbar(Board& b) {
        if (ImGui::Button("+ Note")) addNote(0, 0, false);
        ImGui::SameLine();
        if (ImGui::Button("+ Image…")) dialog_.openFile(host_.window(), "Images", "png;jpg;jpeg;bmp;gif");
        ImGui::SameLine();
        if (ImGui::Button("Pin…")) openPin_ = true;
        ImGui::SameLine(0, U(24));
        if (ImGui::Button("Fit")) {
            MasterScreenData::fit(b, canvasUnits_.x, canvasUnits_.y);
            markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("-")) zoomAbout(b, b.zoom / 1.25f, {canvasUnits_.x * 0.5f, canvasUnits_.y * 0.5f});
        ImGui::SameLine();
        char zoomLabel[16];
        std::snprintf(zoomLabel, sizeof zoomLabel, "%d%%##zoom", static_cast<int>(std::lround(b.zoom * 100)));
        if (ImGui::Button(zoomLabel)) zoomAbout(b, 1.0f, {canvasUnits_.x * 0.5f, canvasUnits_.y * 0.5f});
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to go back to 100%%.");
        ImGui::SameLine();
        if (ImGui::Button("+")) zoomAbout(b, b.zoom * 1.25f, {canvasUnits_.x * 0.5f, canvasUnits_.y * 0.5f});
        ImGui::SameLine(0, U(24));
        if (ImGui::Checkbox("Lock", &b.locked)) markDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock the board: items cannot be moved, resized or closed by accident.");
        if (!undo_.empty()) {
            ImGui::SameLine(0, U(24));
            if (ImGui::Button("Undo remove")) undoRemove();
        }
        ImGui::SameLine(0, U(24));
        ImGui::TextColored(kGrey, "(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Wheel: zoom. Drag empty space or use the middle button: pan.\nDrag a title bar to move, the corner to resize, right-click a title for more.\nDrop an image file on the window to pin it. Delete removes the selected item, Ctrl+Z brings it back.");
    }

    // ---------------------------------------------------------------------------------------------- canvas
    void drawCanvas(Board& b) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##canvas", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 p1(p0.x + size.x, p0.y + size.y);
        origin_ = p0;
        canvasPx_ = size;
        canvasUnits_ = {size.x / ui_, size.y / ui_};
        if (fitPending_) {
            MasterScreenData::fit(b, canvasUnits_.x, canvasUnits_.y);
            fitPending_ = false;
        }

        // wheel zooms around the pointer over empty canvas (or anywhere with Ctrl); a middle-button drag pans over anything
        const bool overBackground = ImGui::IsWindowHovered();
        const bool overCanvas = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        if (io.MouseWheel != 0 && (overBackground || (overCanvas && io.KeyCtrl)))
            zoomAbout(b, b.zoom * std::pow(1.12f, io.MouseWheel), {(io.MousePos.x - p0.x) / ui_, (io.MousePos.y - p0.y) / ui_});
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning_ = true;
        if (panning_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                b.viewX += io.MouseDelta.x / ui_;
                b.viewY += io.MouseDelta.y / ui_;
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                markDirty();
            } else {
                panning_ = false;
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(214, 201, 170, 255));
        dl->PushClipRect(p0, p1, true);
        drawGrid(dl, b, p0, p1);
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##background", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            b.viewX += io.MouseDelta.x / ui_;
            b.viewY += io.MouseDelta.y / ui_;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            markDirty();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selected_.clear();
            renaming_.clear();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            openBackgroundMenu_ = true;
            menuPoint_ = {((io.MousePos.x - p0.x) / ui_ - b.viewX) / b.zoom, ((io.MousePos.y - p0.y) / ui_ - b.viewY) / b.zoom};
        }

        // items, back to front: each is a child window, so later ones sit on top
        std::vector<ScreenItem*> order;
        for (ScreenItem& it : b.items) order.push_back(&it);
        std::ranges::stable_sort(order, [](const ScreenItem* a, const ScreenItem* c) { return a->z < c->z; });
        for (ScreenItem* it : order) drawItem(b, *it, p0, p1);

        if (b.items.empty())
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + U(24), p0.y + U(20)), IM_COL32(125, 108, 84, 255),
                        "An empty board. Pin things with the Pin buttons around the app, add notes, or drop an image file here.");
        dl->PopClipRect();

        if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undoRemove();
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !selected_.empty()) removeItem(b, selected_);
        }
        ImGui::EndChild();
    }

    void drawGrid(ImDrawList* dl, const Board& b, ImVec2 p0, ImVec2 p1) const {
        float step = 40.0f * b.zoom * ui_;
        while (step < 14.0f * ui_) step *= 4.0f;
        const float ox = std::fmod(p0.x + b.viewX * ui_, step), oy = std::fmod(p0.y + b.viewY * ui_, step);
        const ImU32 dot = IM_COL32(190, 175, 140, 255);
        for (float x = p0.x + (ox < 0 ? ox + step : ox); x < p1.x; x += step)
            for (float y = p0.y + (oy < 0 ? oy + step : oy); y < p1.y; y += step) dl->AddRectFilled(ImVec2(x - 1, y - 1), ImVec2(x + 1, y + 1), dot);
    }

    void removeItem(Board& b, const std::string& id) {
        if (b.locked) return;
        ScreenItem gone;
        if (!data_.removeItem(b, id, &gone)) return;
        undo_.push_back({b.id, std::move(gone)});
        if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
        if (selected_ == id) selected_.clear();
        markDirty();
    }

    void undoRemove() {
        if (undo_.empty()) return;
        Removed r = std::move(undo_.back());
        undo_.pop_back();
        if (Board* b = data_.find(r.boardId)) {
            selected_ = data_.addItem(*b, std::move(r.item)).id;
            markDirty();
        }
    }

    // ----------------------------------------------------------------------------------------------- items
    std::string titleOf(const ScreenItem& it) const {
        std::string t;
        if (it.type == "character") {
            if (const Character* c = host_.characters().find(it.ref)) t = c->displayName();
        } else if (it.type == "party") {
            if (const Party* p = host_.parties().find(it.ref)) t = p->name;
        } else if (it.type != "note" && it.type != "image") {
            Kind k;
            if (kindFromKey(it.kind, k))
                if (const int id = host_.content().idByKey(k, it.ref)) t = host_.content().titleOf(k, id);
        } else {
            t = it.title;
        }
        if (t.empty()) t = it.title;
        return t.empty() ? it.type : t;
    }

    void drawItem(Board& b, ScreenItem& it, ImVec2 p0, ImVec2 p1) {
        const float zoom = b.zoom, k = zoom * ui_;
        const bool lod = zoom < kLodZoom;
        const float th = kTitleH * k;
        const float w = it.w * k, h = (it.collapsed ? kTitleH : it.h) * k;
        const ImVec2 tl(p0.x + (b.viewX + it.x * zoom) * ui_, p0.y + (b.viewY + it.y * zoom) * ui_);
        const ImVec2 br(tl.x + w, tl.y + h);
        if (tl.x > p1.x || tl.y > p1.y || br.x < p0.x || br.y < p0.y) return;

        const ImGuiStyle savedStyle = ImGui::GetStyle();
        if (!lod) {
            ImGui::GetStyle().ScaleAllSizes(zoom);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * zoom);
        }
        const bool selected = it.id == selected_;
        const ImVec4 col = itemColor(it);
        ImGui::PushID(it.id.c_str());
        ImGui::SetCursorScreenPos(tl);
        // The frame paints its own background and border: ImGui draws a child's native background in the parent's layer, under every
        // sibling's contents, which would let an item stacked below show through one stacked above it.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const bool open = ImGui::BeginChild("##item", ImVec2(w, h), ImGuiChildFlags_None,
                                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        if (open) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rounding = 8.0f * k;
            const std::string title = titleOf(it);
            dl->AddRectFilled(tl, br, IM_COL32(241, 233, 211, 255), rounding);
            if (it.type == "note" && !it.collapsed && !lod)
                dl->AddRectFilled(ImVec2(tl.x, tl.y + th), br, u32(col, 1.0f, 0.22f), rounding, ImDrawFlags_RoundCornersBottom);
            dl->AddRectFilled(tl, lod ? br : ImVec2(br.x, tl.y + th), u32(col, 0.80f), rounding, lod || it.collapsed ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);

            const float btn = th;
            const float dragW = lod ? w : w - (b.locked ? 0.0f : btn) - btn;
            if (renaming_ == it.id && !lod) {
                ImGui::SetCursorScreenPos(ImVec2(tl.x + 4 * k, tl.y + 2 * k));
                ImGui::SetNextItemWidth(std::max(20.0f, dragW - 8 * k));
                if (renameFocus_) {
                    ImGui::SetKeyboardFocusHere();
                    renameFocus_ = false;
                }
                const bool enter = ImGui::InputText("##rename", renameBuf_, sizeof renameBuf_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) renaming_.clear();
                else if (enter || ImGui::IsItemDeactivated()) {
                    it.title = renameBuf_;
                    renaming_.clear();
                    markDirty();
                }
            } else {
                ImGui::SetCursorScreenPos(tl);
                ImGui::InvisibleButton("##drag", ImVec2(std::max(1.0f, dragW), lod ? h : th));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && !b.locked) {
                    it.x += ImGui::GetIO().MouseDelta.x / k;
                    it.y += ImGui::GetIO().MouseDelta.y / k;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    markDirty();
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    selected_ = it.id;
                    front_ = it.id;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    selected_ = it.id;
                    front_ = it.id;
                    menuItem_ = it.id;
                    openItemMenu_ = true;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !lod) {
                    it.collapsed = !it.collapsed;
                    markDirty();
                }
            }

            // title text, clipped so it never runs under the buttons
            const float fs = ImGui::GetFontSize();
            dl->PushClipRect(ImVec2(tl.x + 2, tl.y), ImVec2(tl.x + std::max(4.0f, dragW), lod ? br.y : tl.y + th), true);
            if (!(renaming_ == it.id && !lod)) {
                if (lod) {
                    dl->AddText(ImGui::GetFont(), fs * 1.15f, ImVec2(tl.x + 8 * ui_, tl.y + 6 * ui_), IM_COL32_WHITE, title.c_str(), nullptr, w - 16 * ui_);
                    dl->AddText(ImGui::GetFont(), fs * 0.85f, ImVec2(tl.x + 8 * ui_, br.y - fs * 1.4f), IM_COL32(230, 230, 230, 200), it.type.c_str());
                } else {
                    dl->AddText(ImGui::GetFont(), fs, ImVec2(tl.x + 8 * k, tl.y + (th - fs) * 0.5f), IM_COL32_WHITE, title.c_str());
                }
            }
            dl->PopClipRect();

            if (!lod) {
                // collapse and close buttons
                float bx = br.x - btn;
                if (!b.locked) {
                    ImGui::SetCursorScreenPos(ImVec2(bx, tl.y));
                    if (ImGui::InvisibleButton("##close", ImVec2(btn, th))) closeRequest_ = it.id;
                    const bool hov = ImGui::IsItemHovered();
                    const ImVec2 c(bx + btn * 0.5f, tl.y + th * 0.5f);
                    const float r = btn * 0.17f;
                    if (hov) dl->AddRectFilled(ImVec2(bx, tl.y), ImVec2(bx + btn, tl.y + th), IM_COL32(255, 255, 255, 40), rounding, ImDrawFlags_RoundCornersTopRight);
                    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32_WHITE, 1.6f * ui_);
                    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), IM_COL32_WHITE, 1.6f * ui_);
                    bx -= btn;
                }
                ImGui::SetCursorScreenPos(ImVec2(bx, tl.y));
                if (ImGui::InvisibleButton("##fold", ImVec2(btn, th))) {
                    it.collapsed = !it.collapsed;
                    markDirty();
                }
                {
                    const bool hov = ImGui::IsItemHovered();
                    const ImVec2 c(bx + btn * 0.5f, tl.y + th * 0.5f);
                    const float r = btn * 0.17f;
                    if (hov) dl->AddRectFilled(ImVec2(bx, tl.y), ImVec2(bx + btn, tl.y + th), IM_COL32(255, 255, 255, 40));
                    if (it.collapsed) dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x, c.y + r), IM_COL32_WHITE);
                    else dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), IM_COL32_WHITE, 1.6f * ui_);
                }

                if (!it.collapsed) drawGrip(b, it, br, k, dl);
            }
            dl->AddRect(tl, br, selected ? u32(col) : IM_COL32(185, 168, 132, 255), rounding, (selected ? 2.0f : 1.0f) * ui_);
            touched(ImGui::IsWindowHovered(), it.id);
        }
        ImGui::EndChild();

        // The body is a sibling of the frame, right after it (ImGui draws nested child windows after the parent's siblings, which
        // would put every body above the frame of an item stacked over it).
        if (open && !lod && !it.collapsed) {
            ImGui::SetCursorScreenPos(ImVec2(tl.x, tl.y + th));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * k, 8.0f * k));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            const bool fixed = it.type == "note" || it.type == "image";
            const bool bodyOpen = ImGui::BeginChild("##body", ImVec2(w, h - th - kGripH * k), ImGuiChildFlags_AlwaysUseWindowPadding,
                                                    fixed ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            if (bodyOpen) {
                touched(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows), it.id);
                drawBody(it);
            }
            ImGui::EndChild();
        }
        ImGui::PopID();
        if (!lod) {
            ImGui::PopFont();
            ImGui::GetStyle() = savedStyle;
        }
    }

    // A click anywhere on an item selects it and brings it to the front (applied once the frame is done).
    void touched(bool hovered, const std::string& id) {
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            selected_ = id;
            front_ = id;
        }
    }

    void drawGrip(Board& b, ScreenItem& it, ImVec2 br, float k, ImDrawList* dl) {
        if (b.locked) return;
        const float g = kGripH * k;
        ImGui::SetCursorScreenPos(ImVec2(br.x - g - 2 * k, br.y - g - 2 * k));
        ImGui::InvisibleButton("##size", ImVec2(g, g));
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            it.w = std::max(MasterScreenData::kMinItemW, it.w + ImGui::GetIO().MouseDelta.x / k);
            it.h = std::max(MasterScreenData::kMinItemH, it.h + ImGui::GetIO().MouseDelta.y / k);
            markDirty();
        }
        const ImU32 c = hot ? IM_COL32(220, 224, 230, 255) : IM_COL32(110, 116, 124, 255);
        for (int i = 1; i <= 3; ++i) {
            const float o = static_cast<float>(i) * g * 0.28f;
            dl->AddLine(ImVec2(br.x - 3 * k, br.y - o - 3 * k), ImVec2(br.x - o - 3 * k, br.y - 3 * k), c, 1.2f * ui_);
        }
    }

    // What is inside an item, drawn in a child window whose font and style are already scaled.
    void drawBody(ScreenItem& it) {
        if (it.type == "note") {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            if (inputMultiline("##text", it.text, ImGui::GetContentRegionAvail())) markDirty();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        } else if (it.type == "image") {
            drawImage(it);
        } else if (it.type == "character") {
            drawCharacter(it);
        } else if (it.type == "party") {
            drawParty(it);
        } else {
            Kind kind;
            const int id = kindFromKey(it.kind, kind) ? host_.content().idByKey(kind, it.ref) : 0;
            if (id == 0) missing(it);
            else if (it.type == "creature") drawCreature(it, id);
            else if (it.type == "table") drawTable(it, id);
            else drawEntry(kind, id);
        }
    }

    void missing(const ScreenItem& it) const {
        ImGui::TextColored(kGrey, "Not available any more (its content pack was removed or switched off).");
        if (!it.title.empty()) ImGui::TextWrapped("%s", it.title.c_str());
    }

    void drawImage(const ScreenItem& it) {
        const TextureCache::Tex* t = it.image.empty() ? nullptr : host_.textures().get(it.image);
        if (!t) {
            ImGui::TextColored(kGrey, "Image not found:");
            ImGui::TextWrapped("%s", it.image.c_str());
            return;
        }
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float scale = std::min(avail.x / static_cast<float>(t->w), avail.y / static_cast<float>(t->h));
        const ImVec2 sz(static_cast<float>(t->w) * scale, static_cast<float>(t->h) * scale);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - sz.x) * 0.5f);
        ImGui::Image(reinterpret_cast<ImTextureID>(t->tex), sz);
    }

    void drawEntry(Kind kind, int id) {
        const Entry* e = host_.content().entry(kind, id);
        if (!e) return;
        sourceBadge(host_, e->sourceId);
        if (!e->subtitle.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "· %s", e->subtitle.c_str());
        }
        fieldsTable(e->fields, "##fields");
        ImGui::Spacing();
        paragraphs(e->body);
        for (size_t i = 0; i < e->tables.size(); ++i) {                    // the card's own tables (a kin's first names)
            ImGui::PushID(static_cast<int>(i));
            ImGui::Spacing();
            bigText(e->tables[i].title.c_str(), 1.1f, kAccent);
            tableGrid(e->tables[i], -1);
            ImGui::PopID();
        }
        pageLink(host_, e->sourceId, e->ref, e->pageNote);
    }

    void drawCreature(const ScreenItem& it, int id) {
        const Monster* mp = host_.content().monster(id);
        if (!mp) return;
        const Monster& m = *mp;
        sourceBadge(host_, m.sourceId);
        if (!m.category.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "· %s", m.category.c_str());
        }
        if (const TextureCache::Tex* art = m.image.empty() ? nullptr : host_.textures().get(m.image)) {
            const float wd = std::min(ImGui::GetContentRegionAvail().x, U(300));
            ImGui::Image(reinterpret_cast<ImTextureID>(art->tex), ImVec2(wd, wd * static_cast<float>(art->h) / static_cast<float>(art->w)));
        }
        IEncounterSink* encounter = serviceOf<IEncounterSink>(host_);
        for (size_t i = 0; i < m.blocks.size(); ++i) {
            const StatBlock& sb = m.blocks[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Spacing();
            bigText(sb.variant.empty() ? "Stat block" : ("Stat block · " + sb.variant).c_str(), 1.1f, kAccent);
            if (encounter && ImGui::SmallButton("Add to encounter")) encounter->addCreature(m, &sb);
            fieldsTable(sb.fields, "##stat");
            ImGui::PopID();
        }
        auto& rolled = rolled_.try_emplace(it.id, std::pair{-1, 0}).first->second;
        attacksTable(host_, m, rolled.first, rolled.second);
        if (!m.abilities.empty()) {
            ImGui::Spacing();
            bigText("Abilities", 1.1f, kAccent);
            ImGui::PushID("abilities");
            for (size_t i = 0; i < m.abilities.size(); ++i) {
                const NamedText& a = m.abilities[i];
                ImGui::TextColored(kGold, "%s", a.name.c_str());
                ImGui::SameLine(0, 6);
                copyableText(("##a" + std::to_string(i)).c_str(), a.text);
            }
            ImGui::PopID();
        }
        if (!m.description.empty()) {
            ImGui::Separator();
            paragraphs(m.description);
        }
    }

    void drawTable(const ScreenItem& it, int id) {
        auto found = tables_.find(it.id);
        if (found == tables_.end()) {
            TableView v;
            if (const DataTable* t = host_.content().packTable(id)) {
                v.table = *t;
                v.ok = true;
            }
            found = tables_.emplace(it.id, std::move(v)).first;
        }
        TableView& v = found->second;
        if (!v.ok) return missing(it);
        if (const int sides = v.table.dieSides(); sides > 0) {
            if (ImGui::Button(("Roll " + v.table.dice).c_str())) {
                v.roll = host_.dice().roll(sides);
                v.row = v.table.rowForRoll(v.roll);
                host_.flashRoll();
            }
            if (v.roll > 0) {
                ImGui::SameLine();
                ImGui::TextColored(kGold, "Rolled %d", v.roll);
            }
        }
        tableGrid(v.table, v.row);
    }

    // A character card: hit and willpower points can be changed right here (saved to the character's file).
    void drawCharacter(const ScreenItem& it) {
        Character* c = host_.characters().find(it.ref);
        if (!c) return missing(it);
        ImGui::TextColored(kGrey, "%s", (c->kin.name + (c->profession.name.empty() ? "" : " · " + c->profession.name)).c_str());
        if (!c->player.empty()) ImGui::TextColored(kGrey, "Player: %s", c->player.c_str());
        bool changed = false;
        auto points = [&](const char* label, int& value, int maximum, ImVec4 color) {
            ImGui::PushID(label);
            ImGui::TextColored(color, "%s", label);
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) value = std::max(0, value - 1), changed = true;
            ImGui::SameLine();
            char text[24];
            std::snprintf(text, sizeof text, "%d / %d", value, maximum);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
            ImGui::ProgressBar(maximum > 0 ? static_cast<float>(value) / static_cast<float>(maximum) : 0.0f, ImVec2(std::max(U(60), ImGui::GetContentRegionAvail().x - U(48)), 0), text);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("+")) value = std::min(maximum, value + 1), changed = true;
            ImGui::PopID();
        };
        points("HP", c->hp, maxHp(*c), kRed);
        points("WP", c->wp, maxWp(*c), ImVec4(0.35f, 0.55f, 0.95f, 1));
        if (changed) host_.characters().save(*c);
        std::string attrs;
        for (int i = 0; i < kAttrCount; ++i) attrs += std::string(i ? "   " : "") + kAttrShort[i] + " " + std::to_string(c->attr[i]);
        ImGui::Spacing();
        ImGui::TextColored(kGold, "%s", attrs.c_str());
        auto listOf = [&](const char* label, const std::vector<std::string>& names) {
            if (names.empty()) return;
            std::string joined;
            for (const std::string& n : names) joined += (joined.empty() ? "" : ", ") + n;
            ImGui::TextColored(kAccent, "%s", label);
            ImGui::TextWrapped("%s", joined.c_str());
        };
        std::vector<std::string> weapons, abilities, spells;
        for (const Item& w : c->weapons) weapons.push_back(w.name);
        if (!c->armor.name.empty()) weapons.push_back(c->armor.name);
        if (!c->helmet.name.empty()) weapons.push_back(c->helmet.name);
        for (const Ref& r : c->abilities) abilities.push_back(r.name);
        for (const Ref& r : c->spells) spells.push_back(r.name);
        listOf("Weapons and armor", weapons);
        listOf("Abilities", abilities);
        listOf("Spells", spells);
        if (!c->notes.empty()) {
            ImGui::TextColored(kAccent, "Notes");
            ImGui::TextWrapped("%s", c->notes.c_str());
        }
    }

    void drawParty(const ScreenItem& it) {
        const Party* p = host_.parties().find(it.ref);
        if (!p) return missing(it);
        if (!p->notes.empty()) ImGui::TextWrapped("%s", p->notes.c_str());
        ImGui::Spacing();
        for (const std::string& id : p->members) {
            const Character* c = host_.characters().find(id);
            if (!c) continue;
            ImGui::PushID(id.c_str());
            ImGui::TextColored(kGold, "%s", c->displayName().c_str());
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "%s", c->profession.name.c_str());
            char text[24];
            std::snprintf(text, sizeof text, "HP %d/%d", c->hp, maxHp(*c));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kRed);
            ImGui::ProgressBar(static_cast<float>(c->hp) / static_cast<float>(std::max(1, maxHp(*c))), ImVec2(-FLT_MIN, 0), text);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (p->members.empty()) ImGui::TextColored(kGrey, "No characters in this party yet.");
    }

    // ---------------------------------------------------------------------------------------------- popups
    enum class BoardAction { None, Add, Rename, Delete };

    void drawPopups(Board& b) {
        if (openItemMenu_) {
            ImGui::OpenPopup("##itemmenu");
            openItemMenu_ = false;
        }
        if (openBackgroundMenu_) {
            ImGui::OpenPopup("##bgmenu");
            openBackgroundMenu_ = false;
        }
        if (openPin_) {
            ImGui::OpenPopup("##pin");
            openPin_ = false;
            pinFilter_[0] = 0;
            pinHits_.clear();
            pinQuery_.clear();
        }
        if (ImGui::BeginPopup("##itemmenu")) {
            itemMenu(b);
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##bgmenu")) {
            if (ImGui::MenuItem("New note here")) addNote(menuPoint_.x, menuPoint_.y, true);
            if (ImGui::MenuItem("Add image…")) dialog_.openFile(host_.window(), "Images", "png;jpg;jpeg;bmp;gif");
            if (ImGui::MenuItem("Pin something…")) openPin_ = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Fit everything")) {
                MasterScreenData::fit(b, canvasUnits_.x, canvasUnits_.y);
                markDirty();
            }
            if (ImGui::MenuItem("Reset view")) {
                b.viewX = b.viewY = 0;
                b.zoom = 1.0f;
                markDirty();
            }
            ImGui::EndPopup();
        }
        drawPinPopup();
        drawBoardDialogs();
    }

    void itemMenu(Board& b) {
        ScreenItem* it = b.find(menuItem_);
        if (!it) return;
        ImGui::TextColored(kGrey, "%s", titleOf(*it).c_str());
        ImGui::Separator();
        if ((it->type == "note" || it->type == "image") && ImGui::MenuItem("Rename")) {
            renaming_ = it->id;
            renameFocus_ = true;
            std::snprintf(renameBuf_, sizeof renameBuf_, "%s", it->title.c_str());
        }
        ImGui::TextColored(kGrey, "Colour");
        ImGui::SameLine();
        for (int i = 0; i <= 12; ++i) {
            if (i) ImGui::SameLine(0, 3);
            ImGui::PushID(i);
            const ImVec4 c = i == 0 ? typeColor(it->type) : kPalette[i - 1];
            if (ImGui::ColorButton("##c", c, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(U(16), U(16)))) {
                it->color = i;
                markDirty();
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(it->collapsed ? "Expand" : "Collapse")) {
            it->collapsed = !it->collapsed;
            markDirty();
        }
        if (ImGui::MenuItem("Bring to front")) {
            data_.bringToFront(b, it->id);
            markDirty();
        }
        if (ImGui::MenuItem("Send to back")) {
            int low = it->z;
            for (const ScreenItem& o : b.items) low = std::min(low, o.z);
            it->z = low - 1;
            markDirty();
        }
        if (ImGui::MenuItem("Duplicate", nullptr, false, !b.locked)) {
            ScreenItem copy = *it;
            copy.id.clear();
            copy.x += 28;
            copy.y += 28;
            selected_ = data_.addItem(b, std::move(copy)).id;
            markDirty();
            return;
        }
        Kind kind;
        if (it->type == "character") {
            if (ICharacters* ch = serviceOf<ICharacters>(host_))
                if (ImGui::MenuItem("Open the sheet")) {
                    ch->openCharacter(it->ref);
                    host_.showModule("characters");
                }
        } else if (it->type == "party") {
            if (ImGui::MenuItem("Open the party")) host_.showModule("party");
        } else if (it->type != "note" && it->type != "image" && kindFromKey(it->kind, kind)) {
            if (const int id = host_.content().idByKey(kind, it->ref))
                if (ImGui::MenuItem("Open in its section")) host_.goTo(kind, id);
        }
        if (IMessenger* m = serviceOf<IMessenger>(host_)) {
            std::string image = it->type == "image" ? it->image : std::string();
            if (it->type == "creature" && kindFromKey(it->kind, kind))
                if (const Monster* mon = host_.content().monster(host_.content().idByKey(kind, it->ref))) image = mon->image;
            if (!image.empty() && ImGui::MenuItem("Send the picture to the players…")) m->compose("", image, titleOf(*it));
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove", "Del", false, !b.locked)) closeRequest_ = it->id;
    }

    void drawPinPopup() {
        ImGui::SetNextWindowSizeConstraints(ImVec2(U(380), U(120)), ImVec2(U(520), U(520)));
        if (!ImGui::BeginPopup("##pin")) return;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(U(360));
        ImGui::InputTextWithHint("##pinfilter", "Search creatures, spells, tables, rules…", pinFilter_, sizeof pinFilter_);
        if (pinQuery_ != pinFilter_) {
            pinQuery_ = pinFilter_;
            pinHits_ = pinQuery_.size() < 2 ? std::vector<Hit>{} : host_.content().search(pinQuery_, 16);
        }
        if (pinQuery_.size() < 2) {
            if (!host_.characters().all().empty()) ImGui::TextColored(kAccent, "Characters");
            for (const Character& c : host_.characters().all())
                if (ImGui::Selectable((c.displayName() + "##c" + c.id).c_str())) {
                    pinCharacter(c.id);
                    ImGui::CloseCurrentPopup();
                }
            if (!host_.parties().all().empty()) ImGui::TextColored(kAccent, "Parties");
            for (const Party& p : host_.parties().all())
                if (ImGui::Selectable((p.name + "##p" + p.id).c_str())) {
                    pinParty(p.id);
                    ImGui::CloseCurrentPopup();
                }
            if (host_.characters().all().empty() && host_.parties().all().empty()) ImGui::TextColored(kGrey, "Type at least two letters to search the content.");
        }
        for (const Hit& h : pinHits_) {
            ImGui::PushID(&h);
            if (ImGui::Selectable((h.title + "   " + kindLabel(h.kind)).c_str())) {
                pinEntry(h.kind, h.id);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    void drawBoardDialogs() {
        if (boardAction_ == BoardAction::Rename) ImGui::OpenPopup("Rename board");
        if (boardAction_ == BoardAction::Delete) ImGui::OpenPopup("Delete board");
        const BoardAction action = boardAction_;
        if (action == BoardAction::Rename || action == BoardAction::Delete) boardAction_ = BoardAction::None;
        if (ImGui::BeginPopupModal("Rename board", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##name", nameBuf_, sizeof nameBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter || ImGui::Button("OK")) {
                if (Board* t = data_.find(boardTarget_))
                    if (nameBuf_[0]) t->name = nameBuf_;
                markDirty();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete board", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const Board* t = data_.find(boardTarget_);
            ImGui::Text("Delete \"%s\" and its %d items?", t ? t->name.c_str() : "?", t ? static_cast<int>(t->items.size()) : 0);
            if (ImGui::Button("Delete")) {
                data_.removeBoard(boardTarget_);
                selected_.clear();
                tabFrames_ = 2;
                markDirty();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // Changes that would move or invalidate items while they are being drawn wait until the frame is done.
    void applyPending() {
        Board* b = data_.active();
        if (!b) return;
        if (!front_.empty()) {
            const ScreenItem* it = b->find(front_);
            const int before = it ? it->z : 0;
            data_.bringToFront(*b, front_);
            if (it && it->z != before) markDirty();
            front_.clear();
        }
        if (!closeRequest_.empty()) {
            removeItem(*b, closeRequest_);
            closeRequest_.clear();
        }
        if (boardAction_ == BoardAction::Add) {
            data_.addBoard("");
            selected_.clear();
            tabFrames_ = 2;
            markDirty();
            boardAction_ = BoardAction::None;
        }
    }

    MasterScreenData data_;
    FileDialog dialog_;
    bool dirty_ = false;
    Uint64 saveAt_ = 0;

    // view of the last frame (also read by pinImage to place a dropped file)
    float ui_ = 1.0f;
    ImVec2 origin_{0, 0}, canvasPx_{900, 600}, canvasUnits_{900, 600};
    int frameSeen_ = -10;
    bool fitPending_ = false;
    bool panning_ = false;

    std::string selected_, front_, closeRequest_, renaming_, menuItem_;
    char renameBuf_[128] = {};
    bool renameFocus_ = false;
    bool openItemMenu_ = false, openBackgroundMenu_ = false, openPin_ = false;
    ImVec2 menuPoint_{0, 0};

    BoardAction boardAction_ = BoardAction::None;
    std::string boardTarget_;
    char nameBuf_[96] = {};
    int tabFrames_ = 0;

    char pinFilter_[64] = {};
    std::string pinQuery_;
    std::vector<Hit> pinHits_;

    std::vector<Removed> undo_;
    std::map<std::string, std::pair<int, int>> rolled_;
    std::map<std::string, TableView> tables_;
};

}  // namespace

std::unique_ptr<Module> makeMasterScreenModule(Host& host) { return std::make_unique<MasterScreenModule>(host); }

}  // namespace gm
