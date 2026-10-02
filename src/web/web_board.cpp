#include "web/web_board.h"

#include <algorithm>
#include <atomic>
#include <chrono>

#include <SDL3/SDL.h>

#include "parsing/fsutil.h"
#include "parsing/fts.h"

namespace gm {
namespace {

constexpr size_t kMaxItems = 400;
constexpr size_t kMaxArrows = 1000;
constexpr size_t kMaxBytes = 4 * 1024 * 1024;
constexpr size_t kMaxImageBytes = 8 * 1024 * 1024;
constexpr size_t kMaxNameLength = 80;

std::string boardFile(const std::string& prefsDir) { return fs::withoutTrailingSlash(prefsDir) + "/board.json"; }
std::string sessionsDir(const std::string& prefsDir) { return fs::withoutTrailingSlash(prefsDir) + "/sessions"; }
std::string sessionFile(const std::string& prefsDir, const std::string& id) { return sessionsDir(prefsDir) + "/" + id + ".json"; }
std::string campaignsDir(const std::string& prefsDir) { return fs::withoutTrailingSlash(prefsDir) + "/campaigns"; }
std::string campaignFile(const std::string& prefsDir, const std::string& id) { return campaignsDir(prefsDir) + "/" + id + ".json"; }
std::string imagesDir(const std::string& prefsDir) { return fs::withoutTrailingSlash(prefsDir) + "/board-images"; }

json emptyBoard() { return {{"items", json::array()}, {"arrows", json::array()}, {"view", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}; }

BoardResult failure(int status, std::string error) {
    BoardResult r;
    r.status = status;
    r.error = std::move(error);
    return r;
}

// A board as the page sent it, checked and cleaned (only items, arrows and view); on failure `error` says why.
bool cleanBoard(const json& body, json& out, BoardResult& error) {
    const json* items = body.is_object() ? jsonFind(body, "items") : nullptr;
    const json* arrows = body.is_object() ? jsonFind(body, "arrows") : nullptr;
    const json* view = body.is_object() ? jsonFind(body, "view") : nullptr;
    if (!items || !items->is_array() || !arrows || !arrows->is_array()) {
        error = failure(400, "Send the board as {items: [...], arrows: [...], view: {...}}.");
        return false;
    }
    if (items->size() > kMaxItems || arrows->size() > kMaxArrows) {
        error = failure(400, "The board has too many things (" + std::to_string(kMaxItems) + " items and " + std::to_string(kMaxArrows) + " arrows at most).");
        return false;
    }
    for (const json& item : *items)
        if (!item.is_object() || jsonStr(item, "id").empty()) {
            error = failure(400, "Every item of the board needs an id.");
            return false;
        }
    out = emptyBoard();
    out["items"] = *items;
    out["arrows"] = *arrows;
    if (view && view->is_object()) out["view"] = *view;
    if (out.dump().size() > kMaxBytes) {
        error = failure(413, "The board is too big (4 MB at most): remove some pinned things.");
        return false;
    }
    return true;
}

json boardOf(const json& root) {
    json board = emptyBoard();
    if (root.is_object()) {
        for (const char* key : {"items", "arrows"})
            if (const json* list = jsonFind(root, key); list && list->is_array()) board[key] = *list;
        if (const json* view = jsonFind(root, "view"); view && view->is_object()) board["view"] = *view;
    }
    return board;
}

std::string cleanName(const std::string& name) {
    std::string out = trimmed(name);
    if (out.size() > kMaxNameLength) out.resize(kMaxNameLength);
    return out;
}

json readSession(const std::string& prefsDir, const std::string& id) {
    if (!fs::safeId(id)) return nullptr;
    const auto root = jsonLoad(sessionFile(prefsDir, id));
    if (!root || !root->is_object()) return nullptr;
    json s = {{"id", id}, {"campaign", jsonStr(*root, "campaign")}, {"name", jsonStr(*root, "name", id)}, {"created", jsonStr(*root, "created")}, {"ended", jsonStr(*root, "ended")},
              {"reopened", jsonFind(*root, "reopened") && jsonFind(*root, "reopened")->is_number() ? jsonFind(*root, "reopened")->get<int>() : 0}};
    const json* board = jsonFind(*root, "board");
    s["board"] = boardOf(board ? *board : json::object());
    return s;
}

bool writeSession(const std::string& prefsDir, const json& s) {
    SDL_CreateDirectory(sessionsDir(prefsDir).c_str());
    return fs::writeFile(sessionFile(prefsDir, s.value("id", "")), s.dump());
}

std::vector<json> allSessions(const std::string& prefsDir) {
    std::vector<json> out;
    for (const std::string& file : fs::listDir(sessionsDir(prefsDir))) {
        if (!file.ends_with(".json")) continue;
        const json s = readSession(prefsDir, file.substr(0, file.size() - 5));
        if (s.is_object()) out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) {          // newest first; within the same second, the one made last (its id counts up)
        if (a.value("created", "") != b.value("created", "")) return a.value("created", "") > b.value("created", "");
        const std::string ia = a.value("id", ""), ib = b.value("id", "");
        return ia.size() != ib.size() ? ia.size() > ib.size() : ia > ib;
    });
    return out;
}

// ---- the campaigns ----
constexpr size_t kMaxCharacters = 60;

json readCampaign(const std::string& prefsDir, const std::string& id) {
    if (!fs::safeId(id)) return nullptr;
    const auto root = jsonLoad(campaignFile(prefsDir, id));
    if (!root || !root->is_object()) return nullptr;
    json c = {{"id", id}, {"name", jsonStr(*root, "name", id)}, {"created", jsonStr(*root, "created")}, {"characters", json::array()}};
    if (const json* list = jsonFind(*root, "characters"); list && list->is_array())
        for (const json& ch : *list)
            if (ch.is_string() && fs::safeId(ch.get<std::string>())) c["characters"].push_back(ch);
    return c;
}

bool writeCampaign(const std::string& prefsDir, const json& c) {
    SDL_CreateDirectory(campaignsDir(prefsDir).c_str());
    return fs::writeFile(campaignFile(prefsDir, c.value("id", "")), c.dump());
}

std::vector<json> allSessions(const std::string& prefsDir);

std::vector<json> allCampaigns(const std::string& prefsDir) {
    std::vector<json> out;
    for (const std::string& file : fs::listDir(campaignsDir(prefsDir))) {
        if (!file.ends_with(".json")) continue;
        const json c = readCampaign(prefsDir, file.substr(0, file.size() - 5));
        if (c.is_object()) out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) {
        if (a.value("created", "") != b.value("created", "")) return a.value("created", "") > b.value("created", "");
        const std::string ia = a.value("id", ""), ib = b.value("id", "");
        return ia.size() != ib.size() ? ia.size() > ib.size() : ia > ib;
    });
    return out;
}

std::string newId(const char* prefix) {
    static std::atomic<int> counter{0};
    const long long stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return std::string(prefix) + std::to_string(stamp) + "-" + std::to_string(counter++);
}

// Sessions made before there were campaigns belong to one called "Campaign 1", made now.
void adoptOrphans(const std::string& prefsDir) {
    std::vector<json> orphans;
    for (const json& s : allSessions(prefsDir))
        if (s.value("campaign", "").empty() || readCampaign(prefsDir, s.value("campaign", "")).is_null()) orphans.push_back(s);
    if (orphans.empty()) return;
    std::string oldest = orphans.front().value("created", "");
    for (const json& s : orphans) oldest = std::min(oldest, s.value("created", ""));
    std::string id;
    do id = newId("c");
    while (fs::isFile(campaignFile(prefsDir, id)));
    json c = {{"id", id}, {"name", "Campaign 1"}, {"created", oldest}, {"characters", json::array()}};
    if (!writeCampaign(prefsDir, c)) return;
    for (json s : orphans) {
        s["campaign"] = id;
        const std::string file = sessionFile(prefsDir, s.value("id", ""));
        SDL_CreateDirectory(sessionsDir(prefsDir).c_str());
        fs::writeFile(file, s.dump());
    }
}

// The active session ("" when there is none): the newest of the ones that have not ended.
std::string activeId(const std::vector<json>& sessions) {
    for (const json& s : sessions)
        if (s.value("ended", "").empty()) return s.value("id", "");
    return std::string();
}

// Ends the active session, if there is one, except `keep`.
void endActive(const std::string& prefsDir, const std::string& now, const std::string& keep) {
    for (json s : allSessions(prefsDir))
        if (s.value("ended", "").empty() && s.value("id", "") != keep) {
            s["ended"] = now;
            writeSession(prefsDir, s);
        }
}

// What kind of picture it is, from its first bytes ("" when it is none of the ones we keep).
std::string imageExtension(std::string_view b) {
    if (b.size() >= 8 && b.substr(0, 4) == "\x89PNG") return "png";
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xD8 && static_cast<unsigned char>(b[2]) == 0xFF) return "jpg";
    if (b.size() >= 6 && (b.substr(0, 6) == "GIF87a" || b.substr(0, 6) == "GIF89a")) return "gif";
    if (b.size() >= 12 && b.substr(0, 4) == "RIFF" && b.substr(8, 4) == "WEBP") return "webp";
    return std::string();
}

}  // namespace

// ------------------------------------------------------------------------------------------------ the Screen
json boardLoad(const std::string& prefsDir) {
    const auto root = jsonLoad(boardFile(prefsDir));
    return root ? boardOf(*root) : emptyBoard();
}

BoardResult boardSave(const std::string& prefsDir, const json& body) {
    json board;
    BoardResult r;
    if (!cleanBoard(body, board, r)) return r;
    if (!fs::writeFile(boardFile(prefsDir), board.dump())) return failure(500, "Could not save the board.");
    return r;
}

// ------------------------------------------------------------------------------------------------ the campaigns
json campaignList(const std::string& prefsDir) {
    adoptOrphans(prefsDir);
    const std::vector<json> sessions = allSessions(prefsDir);
    json list = json::array();
    for (const json& c : allCampaigns(prefsDir)) {
        size_t n = 0;
        for (const json& s : sessions) n += s.value("campaign", "") == c.value("id", "");
        json item = c;
        item["sessions"] = n;
        list.push_back(item);
    }
    return {{"campaigns", list}};
}

BoardResult campaignCreate(const std::string& prefsDir, const std::string& name, const std::string& now) {
    const std::string clean = cleanName(name);
    std::string id;
    do id = newId("c");
    while (fs::isFile(campaignFile(prefsDir, id)));
    json c = {{"id", id}, {"name", clean.empty() ? "Campaign " + std::to_string(allCampaigns(prefsDir).size() + 1) : clean}, {"created", now}, {"characters", json::array()}};
    if (!writeCampaign(prefsDir, c)) return failure(500, "Could not start the campaign.");
    BoardResult r;
    r.status = 201;
    r.body = c;
    return r;
}

BoardResult campaignSave(const std::string& prefsDir, const std::string& id, const json& body) {
    json c = readCampaign(prefsDir, id);
    if (c.is_null()) return failure(404, "There is no such campaign.");
    if (!body.is_object()) return failure(400, "Send the campaign as JSON.");
    if (const json* name = jsonFind(body, "name")) {
        const std::string clean = name->is_string() ? cleanName(name->get<std::string>()) : std::string();
        if (clean.empty()) return failure(400, "A campaign needs a name.");
        c["name"] = clean;
    }
    if (const json* characters = jsonFind(body, "characters")) {
        if (!characters->is_array() || characters->size() > kMaxCharacters) return failure(400, "characters must be a list of at most 60 character ids.");
        json list = json::array();
        for (const json& ch : *characters) {
            if (!ch.is_string() || !fs::safeId(ch.get<std::string>())) return failure(400, "A character is its id.");
            if (std::find(list.begin(), list.end(), ch) == list.end()) list.push_back(ch);
        }
        c["characters"] = list;
    }
    if (!writeCampaign(prefsDir, c)) return failure(500, "Could not save the campaign.");
    return BoardResult();
}

BoardResult campaignDelete(const std::string& prefsDir, const std::string& id) {
    if (!fs::safeId(id) || !fs::isFile(campaignFile(prefsDir, id))) return failure(404, "There is no such campaign.");
    for (const json& s : allSessions(prefsDir))
        if (s.value("campaign", "") == id) SDL_RemovePath(sessionFile(prefsDir, s.value("id", "")).c_str());
    if (!SDL_RemovePath(campaignFile(prefsDir, id).c_str())) return failure(500, "Could not delete the campaign.");
    return BoardResult();
}

// ------------------------------------------------------------------------------------------------ the sessions
json sessionList(const std::string& prefsDir) {
    adoptOrphans(prefsDir);
    const std::vector<json> all = allSessions(prefsDir);
    json list = json::array();
    for (const json& s : all)
        list.push_back({{"id", s["id"]}, {"campaign", s["campaign"]}, {"name", s["name"]}, {"created", s["created"]}, {"ended", s["ended"]}, {"items", s["board"]["items"].size()}});
    return {{"sessions", list}, {"active", activeId(all)}};
}

json sessionGet(const std::string& prefsDir, const std::string& id) { return readSession(prefsDir, id); }

BoardResult sessionCreate(const std::string& prefsDir, const std::string& campaign, const std::string& name, const std::string& now) {
    if (campaign.empty() || readCampaign(prefsDir, campaign).is_null()) return failure(400, "A session belongs to a campaign: choose one, or start a campaign first.");
    const std::vector<json> all = allSessions(prefsDir);
    size_t inCampaign = 0;
    for (const json& s : all) inCampaign += s.value("campaign", "") == campaign;
    endActive(prefsDir, now, "");
    static std::atomic<int> counter{0};
    const long long stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string id;
    do id = "s" + std::to_string(stamp) + "-" + std::to_string(counter++);
    while (fs::isFile(sessionFile(prefsDir, id)));
    const std::string clean = cleanName(name);
    json s = {{"id", id}, {"campaign", campaign}, {"name", clean.empty() ? "Session " + std::to_string(inCampaign + 1) : clean}, {"created", now}, {"ended", ""}, {"reopened", 0}};
    s["board"] = emptyBoard();
    if (!writeSession(prefsDir, s)) return failure(500, "Could not start the session.");
    BoardResult r;
    r.status = 201;
    r.body = s;
    return r;
}

BoardResult sessionSave(const std::string& prefsDir, const std::string& id, const json& body) {
    json s = readSession(prefsDir, id);
    if (s.is_null()) return failure(404, "There is no such session.");
    if (!body.is_object()) return failure(400, "Send the session as JSON.");
    if (const json* name = jsonFind(body, "name")) {
        const std::string clean = name->is_string() ? cleanName(name->get<std::string>()) : std::string();
        if (clean.empty()) return failure(400, "A session needs a name.");
        s["name"] = clean;
    }
    if (const json* board = jsonFind(body, "board")) {
        if (!s.value("ended", "").empty()) return failure(409, "That session is closed: open it to change it.");
        json cleaned;
        BoardResult bad;
        if (!cleanBoard(*board, cleaned, bad)) return bad;
        s["board"] = cleaned;
    }
    if (!writeSession(prefsDir, s)) return failure(500, "Could not save the session.");
    return BoardResult();
}

BoardResult sessionEnd(const std::string& prefsDir, const std::string& id, const std::string& now) {
    json s = readSession(prefsDir, id);
    if (s.is_null()) return failure(404, "There is no such session.");
    if (s.value("ended", "").empty()) {
        s["ended"] = now;
        if (!writeSession(prefsDir, s)) return failure(500, "Could not end the session.");
    }
    return BoardResult();
}

BoardResult sessionOpen(const std::string& prefsDir, const std::string& id, const std::string& now) {
    json s = readSession(prefsDir, id);
    if (s.is_null()) return failure(404, "There is no such session.");
    endActive(prefsDir, now, id);
    if (!s.value("ended", "").empty()) {
        s["ended"] = "";
        s["reopened"] = s.value("reopened", 0) + 1;
        if (!writeSession(prefsDir, s)) return failure(500, "Could not open the session.");
    }
    return BoardResult();
}

BoardResult sessionDelete(const std::string& prefsDir, const std::string& id) {
    if (!fs::safeId(id) || !fs::isFile(sessionFile(prefsDir, id))) return failure(404, "There is no such session.");
    if (!SDL_RemovePath(sessionFile(prefsDir, id).c_str())) return failure(500, "Could not delete the session.");
    return BoardResult();
}

// ------------------------------------------------------------------------------------------------ pictures
BoardResult boardImageSave(const std::string& prefsDir, std::string_view bytes) {
    const std::string ext = imageExtension(bytes);
    if (ext.empty()) return failure(400, "The picture must be a PNG, JPEG, GIF or WebP file.");
    if (bytes.size() > kMaxImageBytes) return failure(413, "The picture is too big (8 MB at most).");
    static std::atomic<int> counter{0};
    const long long stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string id = "i" + std::to_string(stamp) + "-" + std::to_string(counter++) + "." + ext;
    SDL_CreateDirectory(imagesDir(prefsDir).c_str());
    if (!fs::writeFile(imagesDir(prefsDir) + "/" + id, bytes)) return failure(500, "Could not keep the picture.");
    BoardResult r;
    r.status = 201;
    r.body = {{"id", id}};
    return r;
}

std::string boardImagePath(const std::string& prefsDir, const std::string& id) {
    if (id.size() < 4 || id.size() > 64 || id[0] != 'i' || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.") != std::string::npos || id.find("..") != std::string::npos) return std::string();
    const std::string path = imagesDir(prefsDir) + "/" + id;
    return fs::isFile(path) ? path : std::string();
}

}  // namespace gm
