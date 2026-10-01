#include "web/web_app.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include <SDL3/SDL.h>

#include "parsing/fsutil.h"
#include "parsing/fts.h"
#include "game/sheet_edit.h"
#include "game/settings.h"
#include "web/web_views.h"
#include "web/web_creation.h"
#include "web/web_adventures.h"
#include "web/web_homebrew.h"

namespace gm {
namespace {

bool pathInfo(const std::string& p, SDL_PathInfo& info) { return SDL_GetPathInfo(p.c_str(), &info); }

WebResponse jsonResponse(int status, const json& body) {
    WebResponse r;
    r.status = status;
    r.body = body.dump();
    r.headers["Cache-Control"] = "no-store";
    return r;
}

WebResponse errorResponse(int status, const std::string& message) { return jsonResponse(status, {{"error", message}}); }

std::string mimeOf(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    static const std::map<std::string, std::string> types = {
        {"html", "text/html; charset=utf-8"}, {"js", "text/javascript; charset=utf-8"}, {"mjs", "text/javascript; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},   {"json", "application/json; charset=utf-8"}, {"svg", "image/svg+xml"},
        {"png", "image/png"},                 {"jpg", "image/jpeg"},  {"jpeg", "image/jpeg"}, {"gif", "image/gif"},
        {"ico", "image/x-icon"},              {"webp", "image/webp"}, {"woff2", "font/woff2"}, {"woff", "font/woff"},
        {"txt", "text/plain; charset=utf-8"}, {"map", "application/json"}, {"webmanifest", "application/manifest+json"}};
    auto it = types.find(ext);
    return it == types.end() ? "application/octet-stream" : it->second;
}

// Standard base64 (what a browser's FileReader gives), or nothing if it is not.
std::optional<std::string> fromBase64(std::string_view in) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned buffer = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        const size_t v = alphabet.find(c);
        if (v == std::string::npos) return std::nullopt;
        buffer = (buffer << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

constexpr size_t kSmallBody = 64 * 1024;                          // every request but a picture is tiny
constexpr size_t kPictureBody = 12 * 1024 * 1024;                 // 8 MB of picture, in base64
constexpr size_t kWritesPerWindow = 60;
constexpr long long kWriteWindowMs = 10 * 1000;

json chatMessageView(const Message& m) {
    return {{"id", m.id}, {"at", m.at}, {"from", m.from}, {"kind", m.kind}, {"to", m.to}, {"text", m.text}, {"image", m.image.empty() ? json(nullptr) : json("/chat/" + m.id + "/image")}};
}

json chatView(const Thread& t) {
    json list = json::array();
    for (const Message& m : t.messages) list.push_back(chatMessageView(m));
    return {{"messages", list}, {"gmRead", t.gmRead}, {"playerRead", t.playerRead}};
}

}  // namespace

template <class T>
void WebApp::DirCache<T>::scan(const std::string& dir, const std::function<bool(const std::string&, T&, const std::string&)>& parse) {
    std::set<std::string> seen;
    for (const std::string& n : fs::listDir(dir)) {
        if (!n.ends_with(".yaml")) continue;
        const std::string id = n.substr(0, n.size() - 5);
        SDL_PathInfo info{};
        if (!fs::safeId(id) || !pathInfo(dir + "/" + n, info) || info.type != SDL_PATHTYPE_FILE) continue;
        seen.insert(id);
        // Files are small and their text is the honest sign of change (file dates are only as fine as the system clock tick).
        const auto text = fs::readFile(dir + "/" + n);
        if (!text) continue;
        const size_t hash = std::hash<std::string>{}(*text);
        const auto it = items.find(id);
        if (it != items.end() && it->second.hash == hash && it->second.size == text->size()) continue;
        // A half-written or damaged file keeps what was read last time (if anything) and is tried again on the next look.
        T doc;
        if (parse(*text, doc, id)) items[id] = Item{hash, text->size(), std::move(doc)};
    }
    std::erase_if(items, [&](const auto& entry) { return !seen.contains(entry.first); });
    present = std::move(seen);
}
WebApp::WebApp(WebConfig config, Clock clock)
    : config_(std::move(config)), clock_(std::move(clock)), access_(config_.prefsDir, config_.publicUrl) {
    if (!clock_)
        clock_ = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    if (!looksLikePack(config_.dataDir + "/packs/core")) {
        error_ = "Cannot find the Core pack (" + config_.dataDir + "/packs/core)";
        return;
    }
    packs_ = std::make_unique<PackManager>(config_.dataDir + "/packs/core", config_.prefsDir + "/packs");
    chat_.setPrefDir(config_.prefsDir + "/");
    changes_.setPrefDir(config_.prefsDir + "/");
    refresh(clock_());
}

WebApp::~WebApp() = default;

// ------------------------------------------------------------------------------------ files

void WebApp::reloadContentIfChanged() {
    Settings settings;
    settings.open(config_.prefsDir + "/settings.yaml");
    const std::vector<PackSpec> specs = packs_->specs(settings.disabledPacks());
    const std::string sig = packSignature(specs);
    if (sig == contentSignature_) return;
    contentSignature_ = sig;
    content_.load(specs);                                // Core and the enabled packs, exactly as the GM app sees them
}

void WebApp::refresh(long long now) {
    if (!ok()) return;
    if (refreshedAt_ >= 0 && now - refreshedAt_ < config_.refreshMs) return;
    refreshedAt_ = now;
    characters_.scan(config_.prefsDir + "/characters", [](const std::string& text, Character& c, const std::string& id) {
        if (!Character::fromJson(text, c, nullptr)) return false;
        c.id = id;                                       // the file name is the identity
        return true;
    });
    reloadContentIfChanged();
    // every character has a token; new characters get one as soon as they appear
    std::vector<std::string> readable;
    for (const auto& [id, item] : characters_.items) readable.push_back(id);
    access_.sync(readable, characters_.present);
}

std::vector<Character> WebApp::characters() {
    std::lock_guard<std::mutex> lock(mutex_);
    refresh(clock_());
    std::vector<Character> out;
    for (const auto& [id, item] : characters_.items) out.push_back(item.doc);
    std::ranges::sort(out, [](const Character& a, const Character& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
    return out;
}

const Character* WebApp::findCharacter(const std::string& id) { return characters_.find(id); }

std::pair<std::string, std::string> WebApp::nameAndLink(const Character& c) const { return {c.name.empty() ? c.id : c.name, access_.linkFor(c.id)}; }

// ---------------------------------------------------------------------------- guessing

// Remembers failed token attempts per address, so guessing is pointless (tokens are 8 hex digits, so the attempt limit is what makes guessing impractical).
bool WebApp::blocked(const std::string& ip, long long now) {
    auto& list = failures_[ip];
    std::erase_if(list, [&](long long t) { return now - t >= config_.failureWindowMs; });
    return static_cast<int>(list.size()) >= config_.maxFailures;
}

void WebApp::failed(const std::string& ip, long long now) {
    if (failures_.size() > 10000) failures_.clear();     // never let a flood of addresses grow the table without bound
    failures_[ip].push_back(now);
}

// ------------------------------------------------------------------------------- routing

WebResponse WebApp::handle(const WebRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ok()) return errorResponse(503, "The GM's data is not available.");
    WebResponse r = route(request, clock_());
    r.headers["X-Content-Type-Options"] = "nosniff";
    r.headers["Referrer-Policy"] = "no-referrer";
    r.headers["X-Frame-Options"] = "DENY";
    r.headers["Content-Security-Policy"] = "default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'";
    return r;
}

WebResponse WebApp::route(const WebRequest& request, long long now) {
    const bool picture = request.method == "POST" && request.path == "/api/chat";
    if (request.body.size() > (picture ? kPictureBody : kSmallBody)) return errorResponse(413, "That is too big.");
    if (request.path == "/api" || request.path.starts_with("/api/")) return api(request, now);
    return serveStatic(request);
}

WebResponse WebApp::api(const WebRequest& request, long long now) {
    const std::string sub = request.path.size() > 4 ? request.path.substr(4) : std::string();
    if (sub == "/health") return jsonResponse(200, {{"ok", true}});

    // ---- everything below needs a personal token ------------------------------------------------------------
    const std::string ip = request.ip.empty() ? "unknown" : request.ip;
    if (blocked(ip, now)) return errorResponse(429, "Too many attempts. Try again in a few minutes.");
    std::string token;
    if (auto h = request.headers.find("authorization"); h != request.headers.end() && h->second.starts_with("Bearer ")) token = trimmed(h->second.substr(7));
    refresh(now);                                        // a character created a moment ago must already be able to sign in

    // GM token: full access to /api/gm/* routes only
    if (access_.isGm(token)) {
        if (!sub.starts_with("/gm")) return errorResponse(403, "Use /api/gm/* with the GM link.");
        return gmApi(request, now);
    }

    const std::string id = access_.characterFor(token);
    const Character* me = id.empty() ? nullptr : characters_.find(id);
    if (!me) {
        failed(ip, now);
        return errorResponse(401, "This link is not valid. Ask your GM for your personal link.");
    }
    const bool reading = request.method == "GET" || request.method == "HEAD";
    const bool writing = (request.method == "POST" && (sub == "/chat" || sub == "/chat/read")) || (request.method == "PATCH" && sub == "/me");
    if (!reading && !writing) return errorResponse(405, "That is not allowed.");
    if (writing && tooManyWrites(id, now)) return errorResponse(429, "Too many changes at once. Wait a moment.");

    if (sub == "/me") return request.method == "PATCH" ? editSheet(request, id, now, true) : jsonResponse(200, characterView(*me, content_));
    if (sub == "/chat") return request.method == "POST" ? postChat(request, id) : jsonResponse(200, chatView(chat_.thread(id)));
    if (sub == "/chat/read") {
        json body;
        if (!jsonParse(request.body, body, nullptr) || !body.is_object()) return errorResponse(400, "Send {\"upTo\": <message id>}.");
        chat_.markRead(id, false, jsonStr(body, "upTo"));
        return jsonResponse(200, {{"ok", true}});
    }
    if (sub.size() > 12 && sub.starts_with("/chat/") && sub.ends_with("/image")) return chatImage(id, sub.substr(6, sub.size() - 6 - 6));
    if (auto reference = referenceApi(sub, request, "")) return std::move(*reference);
    return errorResponse(404, "Not found");
}

// A picture from the conversation: only if it is in this player's own conversation (the file name comes from there, never from
// the request).
WebResponse WebApp::chatImage(const std::string& characterId, const std::string& messageId) {
    for (const Message& m : chat_.thread(characterId).messages) {
        if (m.id != messageId || m.image.empty()) continue;
        auto bytes = fs::readFile(chat_.mediaFile(m.image));
        if (!bytes) break;
        WebResponse r;
        r.body = std::move(*bytes);
        r.contentType = mimeOf(m.image);
        r.headers["Cache-Control"] = "private, max-age=86400";
        return r;
    }
    return errorResponse(404, "No such picture");
}

WebResponse WebApp::postChat(const WebRequest& request, const std::string& characterId) {
    json body;
    if (!jsonParse(request.body, body, nullptr) || !body.is_object()) return errorResponse(400, "Send {\"text\": ..., \"image\": <base64>}.");
    const json* text = jsonFind(body, "text");
    if (text && !text->is_string()) return errorResponse(400, "The text must be text.");
    std::string bytes;
    if (const json* image = jsonFind(body, "image")) {
        const auto decoded = image->is_string() ? fromBase64(image->get<std::string>()) : std::nullopt;
        if (!decoded) return errorResponse(400, "The picture must be sent as base64.");
        bytes = *decoded;
    }
    std::string error;
    if (!chat_.sendFromPlayer(characterId, text ? text->get<std::string>() : std::string(), bytes, &error)) return errorResponse(400, "Could not send: " + error + ".");
    const Thread& t = chat_.thread(characterId);
    return jsonResponse(201, {{"message", t.messages.empty() ? json(nullptr) : chatMessageView(t.messages.back())}});
}

// Applies a set of field changes to a character. Called with asPlayer=true from the player's PATCH /me,
// and asPlayer=false from the GM's PATCH /gm/characters/:id (no field restrictions).
WebResponse WebApp::editSheet(const WebRequest& request, const std::string& characterId, long long now, bool asPlayer) {
    json body;
    if (!jsonParse(request.body, body, nullptr) || !body.is_object() || !body.contains("set")) return errorResponse(400, "Send {\"set\": {...}} with the fields to change.");
    const sheet::EditResult r = sheet::editFile(config_.prefsDir + "/characters/" + characterId + ".yaml", characterId, body["set"], asPlayer, &changes_);
    if (!r.ok) return errorResponse(r.status, r.error);
    refreshedAt_ = -1;                                   // the next look must see the file we just wrote
    refresh(now);
    return jsonResponse(200, characterView(r.character, content_, !asPlayer));
}

// Somebody typing quickly is fine; a script hammering the server is not.
bool WebApp::tooManyWrites(const std::string& characterId, long long now) {
    auto& list = writes_[characterId];
    std::erase_if(list, [&](long long t) { return now - t >= kWriteWindowMs; });
    if (list.size() >= kWritesPerWindow) return true;
    list.push_back(now);
    return false;
}

// The GM sends a direct message to one player.
WebResponse WebApp::postGmChat(const WebRequest& request, const std::string& characterId) {
    json body;
    if (!jsonParse(request.body, body, nullptr) || !body.is_object()) return errorResponse(400, "Send {\"text\": \"...\"}.");
    const json* text = jsonFind(body, "text");
    if (!text || !text->is_string() || text->get<std::string>().empty()) return errorResponse(400, "Text is required.");
    const std::vector<std::string> ids = {characterId};
    std::string error;
    if (!chat_.sendFromGm(ids, false, "", text->get<std::string>(), "", &error)) return errorResponse(400, "Could not send: " + error + ".");
    const Thread& t = chat_.thread(characterId);
    return jsonResponse(201, {{"message", t.messages.empty() ? json(nullptr) : chatMessageView(t.messages.back())}});
}

// The Reference is the same for everybody who is signed in, GM or player: the content types, the rule chapters, the creatures and the NPC
// lists. `sub` is the path after the API prefix ("/content/spells"); `prefix` is the one the caller came in by ("/gm" or "").
std::optional<WebResponse> WebApp::referenceApi(const std::string& sub, const WebRequest& request, const std::string& prefix) {
    if (request.method != "GET" && request.method != "HEAD") return std::nullopt;
    const auto queryText = [&] {
        std::string q;
        if (auto it = request.query.find("q"); it != request.query.end()) q = it->second.substr(0, 100);
        return q;
    };
    if (sub == "/content") return jsonResponse(200, contentSummary(content_));
    if (sub.starts_with("/content/")) {
        json out;
        if (!contentList(content_, sub.substr(9), queryText(), out)) return errorResponse(404, "Unknown content type.");
        return jsonResponse(200, out);
    }
    if (sub.starts_with("/rules/")) {
        json out;
        if (!rulesChapter(content_, sub.substr(7), out)) return errorResponse(404, "Unknown rules chapter.");
        return jsonResponse(200, out);
    }
    if (sub == "/npcs") return jsonResponse(200, npcLists(content_));
    if (sub == "/creatures") return jsonResponse(200, monsterList(content_, queryText()));
    if (sub.size() > 14 && sub.starts_with("/creatures/") && sub.ends_with("/image")) {
        const std::string key = sub.substr(11, sub.size() - 11 - 6);
        for (const Monster& m : content_.monsters()) {
            if (m.key != key || m.image.empty()) continue;
            auto bytes = fs::readFile(m.image);           // the path comes from the loaded content, never from the request
            if (!bytes) break;
            WebResponse r;
            r.body = std::move(*bytes);
            r.contentType = mimeOf(m.image);
            r.headers["Cache-Control"] = "private, max-age=86400";
            return r;
        }
        return errorResponse(404, "No such picture");
    }
    if (sub.starts_with("/creatures/")) {
        const std::string key = sub.substr(11);
        for (const Monster& m : content_.monsters())
            if (m.key == key) return jsonResponse(200, monsterDetail(content_, m, prefix));
        return errorResponse(404, "Creature not found.");
    }
    return std::nullopt;
}

// Full GM API: list/create/edit/delete characters, read/send chat.
WebResponse WebApp::gmApi(const WebRequest& request, long long now) {
    const std::string sub = request.path.size() > 4 ? request.path.substr(4) : std::string();
    const bool reading = request.method == "GET" || request.method == "HEAD";

    if (sub == "/gm/me") return jsonResponse(200, {{"ok", true}, {"gmLink", access_.gmLink()}});

    // ---- the character creator: what each step offers, and a check of the choices so far ----
    if (sub == "/gm/creation" && reading) return jsonResponse(200, creationCatalog(content_));
    if (sub == "/gm/creation/random" && request.method == "POST") return jsonResponse(200, creationToJson(randomCreation(content_, dice_)));
    if (sub == "/gm/creation/preview" && request.method == "POST") {
        json body;
        if (!jsonParse(request.body, body, nullptr) || !body.is_object()) return errorResponse(400, "Send a JSON object.");
        return jsonResponse(200, creationPreview(creationFromJson(body, content_), content_, dice_));
    }

    // ---- adventures: the packs that have an adventure.yaml, to be read (only the GM has them) ----
    if (sub == "/gm/adventures" && reading) return jsonResponse(200, adventureList(config_.dataDir, config_.prefsDir));
    if (sub.starts_with("/gm/adventures/") && sub.ends_with("/notes") && request.method == "PUT") {       // the GM's note on a part of the adventure
        json body;
        if (!jsonParse(request.body, body, nullptr) || !body.is_object()) return errorResponse(400, "Send the note as JSON.");
        const std::string id = sub.substr(15, sub.size() - 15 - 6);
        const NoteResult saved = saveAdventureNote(config_.dataDir, config_.prefsDir, id, jsonStr(body, "node"), jsonStr(body, "text"));
        return saved.status >= 300 ? errorResponse(saved.status, saved.error) : jsonResponse(200, {{"ok", true}});
    }
    if (sub.starts_with("/gm/adventures/") && sub.ends_with("/image") && reading) {                  // a picture of the adventure
        const std::string id = sub.substr(15, sub.size() - 15 - 6);
        const auto asked = request.query.find("path");
        const std::string file = adventureImagePath(config_.dataDir, config_.prefsDir, id, asked == request.query.end() ? std::string() : asked->second);
        auto bytes = file.empty() ? std::nullopt : fs::readFile(file);
        if (!bytes) return errorResponse(404, "No such picture");
        WebResponse r;
        r.body = std::move(*bytes);
        r.contentType = mimeOf(file);
        r.headers["Cache-Control"] = "private, max-age=86400";
        return r;
    }
    if (sub.starts_with("/gm/adventures/") && reading) {
        const json adventure = adventureDetail(config_.dataDir, config_.prefsDir, sub.substr(15), content_);
        return adventure.is_null() ? errorResponse(404, "There is no such adventure.") : jsonResponse(200, adventure);
    }

    // ---- homebrew: the GM's own pack (creatures, and an entry of every kind of data file of Core) ----
    if (sub == "/gm/homebrew" || sub.starts_with("/gm/homebrew/")) {
        const std::string pack = customPackDir(config_.prefsDir);
        const std::string coreDir = config_.dataDir + "/packs/core";
        const auto respond = [&](const HomebrewResult& r) {
            if (r.status >= 300) return errorResponse(r.status, r.error);
            contentSignature_.clear();                           // what was just written must be read back now
            reloadContentIfChanged();
            return jsonResponse(r.status, r.body);
        };
        const auto bodyJson = [&](json& body) { return jsonParse(request.body, body, nullptr); };
        if (sub == "/gm/homebrew") return reading ? jsonResponse(200, homebrewSchema(config_.prefsDir, coreDir)) : errorResponse(405, "That is not allowed.");
        if (sub == "/gm/homebrew/pack" && request.method == "PUT") {
            json body;
            if (!bodyJson(body) || !body.is_object()) return errorResponse(400, "Send the name as JSON.");
            const HomebrewResult named = setHomebrewPackName(config_.prefsDir, jsonStr(body, "name"));
            if (named.status >= 300) return errorResponse(named.status, named.error);
            contentSignature_.clear();
            reloadContentIfChanged();
            return jsonResponse(200, homebrewSchema(config_.prefsDir, coreDir));
        }
        if (sub == "/gm/homebrew/packs" && reading) return jsonResponse(200, homebrewPacks(config_.prefsDir, coreDir));
        const std::string rest = sub.substr(13);                 // "creatures", "creatures/<id>", "spells/3"...
        const size_t slash = rest.find('/');
        const std::string section = rest.substr(0, slash);
        const std::string id = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
        if (!fs::safeId(section)) return errorResponse(400, "Invalid kind.");
        if (section == "creatures") {
            if (!id.empty() && !fs::safeId(id)) return errorResponse(400, "Invalid creature id.");
            if (reading && id.empty()) return jsonResponse(200, homebrewCreatures(pack));
            if (request.method == "POST" && id.empty()) {
                json body;
                if (!bodyJson(body)) return errorResponse(400, "Send the creature as JSON.");
                return respond(saveHomebrewCreature(pack, "", body));
            }
            if (request.method == "PUT" && !id.empty()) {
                json body;
                if (!bodyJson(body)) return errorResponse(400, "Send the creature as JSON.");
                return respond(saveHomebrewCreature(pack, id, body));
            }
            if (request.method == "DELETE" && !id.empty()) return respond(deleteHomebrewCreature(pack, id));
            return errorResponse(405, "That is not allowed.");
        }
        const bool numeric = !id.empty() && id.size() < 6 && id.find_first_not_of("0123456789") == std::string::npos;
        const auto expected = [&] { const auto it = request.query.find("name"); return it == request.query.end() ? std::string() : it->second; };
        // the kinds of card a house rule can replace (the others can only be copied), and the key such a card has in its pack
        static const std::map<std::string, Kind> kCards = {{"abilities", Kind::Ability}, {"spells", Kind::Spell}, {"skills", Kind::Skill}, {"kin", Kind::Kin}, {"professions", Kind::Profession},
                                                           {"weapons", Kind::Weapon}, {"armor", Kind::Armor}, {"gear", Kind::Gear}};
        const auto keyOfCard = [&](const std::string& packId, const std::string& name) -> std::string {
            const auto card = kCards.find(section);
            if (card == kCards.end()) return std::string();
            for (const Entry& e : content_.entries(card->second))
                if (lowerCopy(e.title) == lowerCopy(name) && e.key.starts_with(packId + "/")) return e.key;
            return std::string();
        };
        if (reading && id.empty()) {
            const auto asked = request.query.find("pack");
            const std::string packId = asked == request.query.end() ? std::string() : asked->second;
            const bool own = packId.empty() || packId == pack.substr(pack.find_last_of('/') + 1);
            const std::string dir = own ? pack : homebrewPackDir(config_.prefsDir, coreDir, packId);
            if (dir.empty()) return errorResponse(404, "There is no such pack.");
            json entries = homebrewEntries(dir, coreDir, section);
            if (entries.is_null()) return errorResponse(404, "There is nothing to make of that kind.");
            if (!own) {                                           // somebody else's pack: to look into, with the key a house rule would replace, and whether one does
                std::set<std::string> replaced;
                for (const json& mine : homebrewEntries(pack, coreDir, section)["entries"])
                    if (mine.is_object() && !jsonStr(mine, "replaces").empty()) replaced.insert(jsonStr(mine, "replaces"));
                for (json& e : entries["entries"]) {
                    const std::string key = e.is_object() ? keyOfCard(packId, e.value("name", "")) : std::string();
                    if (key.empty()) continue;
                    e["key"] = key;
                    e["houseRuled"] = replaced.count(key) > 0;
                }
                entries["canReplace"] = kCards.count(section) > 0;
            }
            return jsonResponse(200, entries);
        }
        if (request.method == "POST" && id.empty()) {
            json body;
            if (!bodyJson(body)) return errorResponse(400, "Send the entry as JSON.");
            std::string replaces;
            json base = nullptr;
            if (const json* rule = jsonFind(body, "houseRuleOf"); rule && rule->is_object()) {   // {pack, name}: this entry takes the place of that one
                const std::string from = jsonStr(*rule, "pack"), name = jsonStr(*rule, "name");
                const std::string dir = homebrewPackDir(config_.prefsDir, coreDir, from);
                replaces = keyOfCard(from, name);
                base = dir.empty() ? json(nullptr) : homebrewOriginal(dir, coreDir, section, name);
                if (replaces.empty() || base.is_null()) return errorResponse(400, "That entry cannot be replaced: it is not one of the cards of a pack.");
                for (const json& mine : homebrewEntries(pack, coreDir, section)["entries"])
                    if (mine.is_object() && jsonStr(mine, "replaces") == replaces) return errorResponse(400, "You already have a house rule for it. Edit that one.");
            }
            return respond(saveHomebrewEntry(pack, coreDir, section, -1, "", body, replaces, base));
        }
        if (request.method == "PUT" && numeric) {
            json body;
            if (!bodyJson(body)) return errorResponse(400, "Send the entry as JSON.");
            return respond(saveHomebrewEntry(pack, coreDir, section, std::stoi(id), expected(), body));
        }
        if (request.method == "DELETE" && numeric) return respond(deleteHomebrewEntry(pack, coreDir, section, std::stoi(id), expected()));
        return errorResponse(405, "That is not allowed.");
    }

    // ---- characters ----
    if (sub == "/gm/characters") {
        if (reading) {
            json list = json::array();
            for (const auto& [id, item] : characters_.items)
                list.push_back(characterSummary(item.doc, content_, access_.linkFor(id)));
            return jsonResponse(200, {{"characters", list}});
        }
        if (request.method == "POST") {
            json body;
            if (!jsonParse(request.body, body, nullptr) || !body.is_object()) body = json::object();
            Character c;
            if (const json* wizard = jsonFind(body, "creation"); wizard && wizard->is_object()) {     // the creator's choices: built by the book's rules
                const Creation choices = creationFromJson(*wizard, content_);
                const std::vector<std::string> problems = validateCreation(choices, content_);
                if (!problems.empty()) return errorResponse(400, problems.front());
                c = buildCharacter(choices, content_, dice_);
            } else {
                c.name = jsonStr(body, "name");
            }
            c.id = fs::stampedId("c");
            c.createdAt = c.updatedAt = nowIso();
            const std::string path = config_.prefsDir + "/characters/" + c.id + ".yaml";
            if (!fs::writeFile(path, c.toJson())) return errorResponse(500, "Could not create character.");
            refreshedAt_ = -1;
            refresh(now);
            return jsonResponse(201, characterSummary(c, content_, access_.linkFor(c.id)));
        }
    }

    static const std::string kChars = "/gm/characters/";
    if (sub.starts_with(kChars)) {
        const std::string rest = sub.substr(kChars.size());
        const auto slash = rest.find('/');
        const std::string cid = slash == std::string::npos ? rest : rest.substr(0, slash);
        if (!fs::safeId(cid)) return errorResponse(400, "Invalid character id.");
        if (reading && slash == std::string::npos) {
            const Character* c = characters_.find(cid);
            if (!c) return errorResponse(404, "No such character.");
            return jsonResponse(200, characterView(*c, content_, true));
        }
        if (request.method == "PATCH" && slash == std::string::npos) {
            if (!characters_.find(cid)) return errorResponse(404, "No such character.");
            return editSheet(request, cid, now, false);
        }
        if (request.method == "DELETE" && slash == std::string::npos) {
            const std::string path = config_.prefsDir + "/characters/" + cid + ".yaml";
            if (!fs::isFile(path)) return errorResponse(404, "No such character.");
            SDL_RemovePath(path.c_str());
            refreshedAt_ = -1;
            refresh(now);
            return jsonResponse(200, {{"ok", true}});
        }
    }

    // ---- chat (GM side) ----
    if (sub == "/gm/chat" && reading) {
        json list = json::array();
        for (const auto& [id, item] : characters_.items) {
            const int unread = chat_.unread(id, true);
            const Thread& t = chat_.thread(id);
            json last = nullptr;
            if (!t.messages.empty()) {
                const Message& m = t.messages.back();
                last = {{"id", m.id}, {"at", m.at}, {"from", m.from}, {"text", m.text.size() > 100 ? m.text.substr(0, 100) : m.text}};
            }
            list.push_back({{"characterId", id}, {"name", item.doc.name.empty() ? id : item.doc.name}, {"unread", unread}, {"last", last}});
        }
        return jsonResponse(200, {{"threads", list}});
    }

    static const std::string kChat = "/gm/chat/";
    if (sub.starts_with(kChat)) {
        const std::string rest = sub.substr(kChat.size());
        const auto slash = rest.find('/');
        const std::string cid = slash == std::string::npos ? rest : rest.substr(0, slash);
        const std::string tail = slash == std::string::npos ? std::string() : rest.substr(slash);
        if (!fs::safeId(cid)) return errorResponse(400, "Invalid character id.");
        if (tail.empty() && reading) {
            const Thread& t = chat_.thread(cid);
            return jsonResponse(200, chatView(t));
        }
        if (tail.empty() && request.method == "POST") return postGmChat(request, cid);
        if (tail == "/read" && request.method == "POST") {
            json body;
            if (!jsonParse(request.body, body, nullptr) || !body.is_object()) return errorResponse(400, "Send {\"upTo\": <message id>}.");
            chat_.markRead(cid, true, jsonStr(body, "upTo"));
            return jsonResponse(200, {{"ok", true}});
        }
    }

    // ---- the Reference: the same routes as the players', under /gm ----
    if (sub.starts_with("/gm/"))
        if (auto reference = referenceApi(sub.substr(3), request, "/gm")) return std::move(*reference);

    // ---- links ----
    if (sub == "/gm/links" && reading) {
        json list = json::array();
        for (const auto& [id, item] : characters_.items)
            list.push_back({{"id", id}, {"name", item.doc.name.empty() ? id : item.doc.name}, {"link", access_.linkFor(id)}});
        return jsonResponse(200, {{"characters", list}, {"gmLink", access_.gmLink()}});
    }

    return errorResponse(404, "Not found.");
}

// ------------------------------------------------------------------------- the Vue client

WebResponse WebApp::serveStatic(const WebRequest& request) {
    WebResponse r;
    if (request.method != "GET" && request.method != "HEAD") {
        r.status = 405;
        r.contentType = "text/plain; charset=utf-8";
        r.body = "Method not allowed";
        return r;
    }
    const std::string index = config_.staticDir + "/index.html";
    if (!fs::isFile(index)) {
        r.contentType = "text/plain; charset=utf-8";
        if (request.path == "/") r.body = "Skaldbok web API is running. Build the client (npm run build in web/) to serve the player pages.";
        else r.status = 404;
        return r;
    }
    const std::string& p = request.path;
    const bool unsafe = p.contains("..") || p.contains('\\') || p.contains(':') || p.contains('\0');
    const std::string candidate = config_.staticDir + p;
    const bool isIndex = unsafe || p.size() <= 1 || !fs::isFile(candidate);          // a deep link falls back to the app itself
    const std::string file = isIndex ? index : candidate;
    auto body = fs::readFile(file);
    if (!body) {
        r.status = 404;
        return r;
    }
    r.body = std::move(*body);
    r.contentType = mimeOf(file);
    r.headers["Cache-Control"] = isIndex ? "no-cache" : "public, max-age=3600";
    return r;
}

}  // namespace gm
