#include "web/web_app.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include <SDL3/SDL.h>

#include "fsutil.h"
#include "fts.h"
#include "sheet_edit.h"
#include "settings.h"
#include "web/web_views.h"

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

// The type files a pack may have: a change to any of them makes the content load again.
const char* const kPackFiles[] = {"manifest", "spells", "abilities", "skills", "kin", "professions", "weapons", "armor", "gear", "tables", "creatures"};

}  // namespace

template <class T>
void WebApp::DirCache<T>::scan(const std::string& dir, const std::function<bool(const std::string&, T&, const std::string&)>& parse) {
    std::set<std::string> seen;
    for (const std::string& n : fs::listDir(dir)) {
        if (!n.ends_with(".json")) continue;
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
    if (!db_.open(config_.dataDir + "/dragonbane.db", &error_)) {
        error_ = "Cannot open " + config_.dataDir + "/dragonbane.db: " + error_;
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
    settings.open(config_.prefsDir + "/settings.json");
    const std::vector<PackSpec> specs = packs_->specs(settings.disabledPacks());
    std::string sig;
    for (const PackSpec& s : specs) {
        sig += s.dir + (s.enabled ? "+" : "-");
        for (const char* f : kPackFiles) {
            SDL_PathInfo info;
            if (pathInfo(s.dir + "/" + f + ".json", info)) sig += std::string(f) + std::to_string(info.modify_time) + ":" + std::to_string(info.size) + ";";
        }
    }
    if (sig == contentSignature_) return;
    contentSignature_ = sig;
    content_.load(db_, specs);                           // Core and the enabled packs, exactly as the GM app sees them
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
    parties_.scan(config_.prefsDir + "/parties", [](const std::string& text, Party& p, const std::string& id) {
        if (!Party::fromJson(text, p, nullptr)) return false;
        p.id = id;
        return true;
    });
    reloadContentIfChanged();
    // every character has a token; new characters get one as soon as they appear
    std::vector<std::string> readable;
    for (const auto& [id, item] : characters_.items) readable.push_back(id);
    access_.sync(readable, characters_.present);
}

const Party* WebApp::partyOf(const std::string& characterId) const {
    for (const auto& [id, item] : parties_.items)
        if (std::ranges::contains(item.doc.members, characterId)) return &item.doc;
    return nullptr;
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

// Remembers failed token attempts per address, so guessing is pointless (tokens are 128 random bits anyway).
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

    if (sub == "/me") return request.method == "PATCH" ? editSheet(request, id, now) : jsonResponse(200, characterView(*me, content_, partyOf(id)));
    if (sub == "/party") {
        const Party* party = partyOf(id);
        json body = {{"party", party ? partyView(*party, [this](const std::string& cid) { return characters_.find(cid); }, id) : json(nullptr)}};
        return jsonResponse(200, body);
    }
    if (sub == "/chat") return request.method == "POST" ? postChat(request, id) : jsonResponse(200, chatView(chat_.thread(id)));
    if (sub == "/chat/read") {
        json body;
        if (!jsonParse(request.body, body, nullptr) || !body.is_object()) return errorResponse(400, "Send {\"upTo\": <message id>}.");
        chat_.markRead(id, false, jsonStr(body, "upTo"));
        return jsonResponse(200, {{"ok", true}});
    }
    if (sub.size() > 12 && sub.starts_with("/chat/") && sub.ends_with("/image")) return chatImage(id, sub.substr(6, sub.size() - 6 - 6));
    if (sub == "/content") return jsonResponse(200, contentSummary(content_));
    if (sub.starts_with("/content/")) {
        std::string q;
        if (auto it = request.query.find("q"); it != request.query.end()) q = it->second.substr(0, 100);
        json out;
        if (!contentList(content_, sub.substr(9), q, out))
            return errorResponse(404, "Unknown type. Choose one of: spells, abilities, skills, kin, professions, weapons, armor, gear");
        return jsonResponse(200, out);
    }
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

// The player changes their own sheet, field by field. Nothing is refused for breaking a rule: it is allowed and flagged for the GM.
WebResponse WebApp::editSheet(const WebRequest& request, const std::string& characterId, long long now) {
    json body;
    if (!jsonParse(request.body, body, nullptr) || !body.is_object() || !body.contains("set")) return errorResponse(400, "Send {\"set\": {...}} with the fields to change.");
    const sheet::EditResult r = sheet::editFile(config_.prefsDir + "/characters/" + characterId + ".json", characterId, body["set"], true, &changes_);
    if (!r.ok) return errorResponse(r.status, r.error);
    refreshedAt_ = -1;                                   // the next look must see the file we just wrote
    refresh(now);
    return jsonResponse(200, characterView(r.character, content_, partyOf(characterId)));
}

// Somebody typing quickly is fine; a script hammering the server is not.
bool WebApp::tooManyWrites(const std::string& characterId, long long now) {
    auto& list = writes_[characterId];
    std::erase_if(list, [&](long long t) { return now - t >= kWriteWindowMs; });
    if (list.size() >= kWritesPerWindow) return true;
    list.push_back(now);
    return false;
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
