#include "master_screen.h"

#include <algorithm>
#include <cstdio>
#include <random>

#include <SDL3/SDL.h>

#include "jsonutil.h"

namespace gm {

ScreenItem* Board::find(const std::string& itemId) {
    for (ScreenItem& it : items)
        if (it.id == itemId) return &it;
    return nullptr;
}

int Board::topZ() const {
    int z = 0;
    for (const ScreenItem& it : items) z = std::max(z, it.z);
    return z;
}

std::string MasterScreenData::newId(const char* prefix) {
    SDL_Time t = 0;
    SDL_GetCurrentTime(&t);
    static std::mt19937 rng(static_cast<unsigned>(t) ^ 0xC2B2AE35u);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%s-%llx-%04x", prefix, static_cast<unsigned long long>(t / 1000000), static_cast<unsigned>(rng() & 0xFFFF));
    return buf;
}

Board& MasterScreenData::ensureBoard() {
    if (boards.empty()) {
        Board b;
        b.id = newId("b");
        b.name = "Master Screen";
        boards.push_back(b);
    }
    if (!find(activeId)) activeId = boards.front().id;
    return *find(activeId);
}

Board* MasterScreenData::active() { return find(activeId); }

Board* MasterScreenData::find(const std::string& id) {
    for (Board& b : boards)
        if (b.id == id) return &b;
    return nullptr;
}

Board& MasterScreenData::addBoard(const std::string& name) {
    Board b;
    b.id = newId("b");
    b.name = name.empty() ? "Board " + std::to_string(boards.size() + 1) : name;
    boards.push_back(b);
    activeId = boards.back().id;
    return boards.back();
}

bool MasterScreenData::removeBoard(const std::string& id) {
    if (boards.size() <= 1) return false;
    auto it = std::ranges::find_if(boards, [&](const Board& b) { return b.id == id; });
    if (it == boards.end()) return false;
    const bool wasActive = it->id == activeId;
    boards.erase(it);
    if (wasActive) activeId = boards.front().id;
    return true;
}

ScreenItem& MasterScreenData::addItem(Board& b, ScreenItem item) {
    if (item.id.empty()) item.id = newId("i");
    item.w = std::max(item.w, kMinItemW);
    item.h = std::max(item.h, kMinItemH);
    item.z = b.topZ() + 1;
    b.items.push_back(std::move(item));
    return b.items.back();
}

bool MasterScreenData::removeItem(Board& b, const std::string& itemId, ScreenItem* removed) {
    auto it = std::ranges::find_if(b.items, [&](const ScreenItem& i) { return i.id == itemId; });
    if (it == b.items.end()) return false;
    if (removed) *removed = *it;
    b.items.erase(it);
    return true;
}

void MasterScreenData::bringToFront(Board& b, const std::string& itemId) {
    ScreenItem* it = b.find(itemId);
    if (!it) return;
    int top = 0;
    for (const ScreenItem& o : b.items)
        if (o.id != itemId) top = std::max(top, o.z);
    if (it->z <= top) it->z = top + 1;                            // already in front: leave the numbers alone
}

bool MasterScreenData::bounds(const Board& b, float& x0, float& y0, float& x1, float& y1) {
    if (b.items.empty()) return false;
    x0 = y0 = 1e30f;
    x1 = y1 = -1e30f;
    for (const ScreenItem& i : b.items) {
        x0 = std::min(x0, i.x);
        y0 = std::min(y0, i.y);
        x1 = std::max(x1, i.x + i.w);
        y1 = std::max(y1, i.y + (i.collapsed ? kMinItemH * 0.5f : i.h));
    }
    return true;
}

float MasterScreenData::clampZoom(float z) { return std::clamp(z, kMinZoom, kMaxZoom); }

void MasterScreenData::fit(Board& b, float viewW, float viewH, float margin) {
    float x0, y0, x1, y1;
    if (!bounds(b, x0, y0, x1, y1) || viewW <= 1 || viewH <= 1) {
        b.viewX = b.viewY = 0;
        b.zoom = 1.0f;
        return;
    }
    const float w = std::max(1.0f, x1 - x0) + 2 * margin, h = std::max(1.0f, y1 - y0) + 2 * margin;
    b.zoom = clampZoom(std::min(std::min(viewW / w, viewH / h), 1.5f));
    b.viewX = viewW * 0.5f - (x0 + x1) * 0.5f * b.zoom;
    b.viewY = viewH * 0.5f - (y0 + y1) * 0.5f * b.zoom;
}

// ------------------------------------------------------------------------------------------ files

std::string MasterScreenData::toJson() const {
    json j;
    j["format"] = 1;
    j["active"] = activeId;
    json list = json::array();
    for (const Board& b : boards) {
        json items = json::array();
        for (const ScreenItem& i : b.items) {
            json o = {{"id", i.id}, {"type", i.type}, {"x", i.x}, {"y", i.y}, {"w", i.w}, {"h", i.h}, {"z", i.z}};
            if (!i.kind.empty()) o["kind"] = i.kind;
            if (!i.ref.empty()) o["ref"] = i.ref;
            if (i.color) o["color"] = i.color;
            if (!i.title.empty()) o["title"] = i.title;
            if (!i.text.empty()) o["text"] = i.text;
            if (!i.image.empty()) o["image"] = i.image;
            if (i.collapsed) o["collapsed"] = true;
            items.push_back(o);
        }
        list.push_back({{"id", b.id}, {"name", b.name}, {"view", {{"x", b.viewX}, {"y", b.viewY}, {"zoom", b.zoom}}}, {"locked", b.locked}, {"items", items}});
    }
    j["boards"] = list;
    return j.dump(2) + "\n";
}

namespace {
float num(const json& o, const char* key, float def) {
    const json* v = jsonFind(o, key);
    return v && v->is_number() ? v->get<float>() : def;
}
}  // namespace

bool MasterScreenData::fromJson(const std::string& text, MasterScreenData& out, std::string* error) {
    json j;
    if (!jsonParse(text, j, error)) return false;
    if (!j.is_object()) {
        if (error) *error = "the Master Screen file must be a JSON object";
        return false;
    }
    MasterScreenData d;
    if (const json* boards = jsonFind(j, "boards"); boards && boards->is_array()) {
        for (const json& jb : *boards) {
            if (!jb.is_object()) continue;
            Board b;
            b.id = jsonStr(jb, "id");
            if (b.id.empty() || d.find(b.id)) b.id = newId("b");
            b.name = jsonStr(jb, "name", "Master Screen");
            b.locked = jsonBool(jb, "locked");
            if (const json* v = jsonFind(jb, "view")) {
                b.viewX = num(*v, "x", 0);
                b.viewY = num(*v, "y", 0);
                b.zoom = clampZoom(num(*v, "zoom", 1.0f));
            }
            if (const json* items = jsonFind(jb, "items"); items && items->is_array()) {
                for (const json& ji : *items) {
                    if (!ji.is_object()) continue;
                    ScreenItem i;
                    i.type = jsonStr(ji, "type");
                    if (i.type.empty()) continue;                       // an item that says nothing about itself cannot be drawn
                    i.id = jsonStr(ji, "id");
                    if (i.id.empty() || b.find(i.id)) i.id = newId("i");
                    i.kind = jsonStr(ji, "kind");
                    i.ref = jsonStr(ji, "ref");
                    i.x = num(ji, "x", 0);
                    i.y = num(ji, "y", 0);
                    i.w = std::max(kMinItemW, num(ji, "w", 320));
                    i.h = std::max(kMinItemH, num(ji, "h", 240));
                    i.z = jsonInt(ji, "z");
                    i.color = std::clamp(jsonInt(ji, "color"), 0, 12);
                    i.title = jsonStr(ji, "title");
                    i.text = jsonStr(ji, "text");
                    i.image = jsonStr(ji, "image");
                    i.collapsed = jsonBool(ji, "collapsed");
                    b.items.push_back(std::move(i));
                }
            }
            d.boards.push_back(std::move(b));
        }
    }
    d.activeId = jsonStr(j, "active");
    d.ensureBoard();
    out = std::move(d);
    return true;
}

bool MasterScreenData::load(const std::string& path) {
    const auto text = fs::readFile(path);
    MasterScreenData d;
    if (!text || !fromJson(*text, d, nullptr)) return false;
    *this = std::move(d);
    return true;
}

bool MasterScreenData::save(const std::string& path, std::string* error) const {
    if (path.empty()) return false;
    if (fs::writeFile(path, toJson())) return true;
    if (error) *error = SDL_GetError();
    return false;
}
}  // namespace gm
