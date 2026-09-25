#include "game/messages.h"

#include <algorithm>
#include <set>

#include <SDL3/SDL.h>

#include "game/character.h"
#include "parsing/fsutil.h"
#include "parsing/jsonutil.h"

namespace gm {
namespace {

json messageJson(const Message& m) {
    json j = {{"id", m.id}, {"at", m.at}, {"from", m.from}, {"kind", m.kind}, {"text", m.text}};
    if (!m.to.empty()) j["to"] = m.to;
    if (!m.image.empty()) j["image"] = m.image;
    return j;
}

Message messageFrom(const json& j) {
    Message m;
    m.id = jsonStr(j, "id");
    m.at = jsonStr(j, "at");
    m.from = jsonStr(j, "from") == "player" ? "player" : "gm";
    m.kind = jsonStr(j, "kind") == "broadcast" && m.from == "gm" ? "broadcast" : "message";
    m.to = jsonStr(j, "to");
    m.text = jsonStr(j, "text");
    m.image = jsonStr(j, "image");
    if (!MessageStore::safeMediaName(m.image)) m.image.clear();      // a hand-edited file cannot point outside media/
    return m;
}

const Thread kEmpty;

}  // namespace

size_t MessageStore::signature(const std::string& characterId) const {
    std::vector<std::string> names = fs::listDir(folder(characterId));
    if (names.empty()) return 0;
    std::ranges::sort(names);
    std::string all;
    for (const std::string& n : names) all += n + "\n";
    for (const char* marker : {"/_gm.read", "/_player.read"}) all += fs::readFile(folder(characterId) + marker).value_or("") + "\n";
    return std::hash<std::string>{}(all);
}

bool MessageStore::safeMediaName(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return false;
    return fs::safeId(name.substr(0, dot)) && std::set<std::string>{"png", "jpg", "gif", "webp"}.contains(name.substr(dot + 1));
}

std::string MessageStore::imageExtension(std::string_view b) {
    if (b.starts_with("\x89PNG\r\n\x1a\n")) return "png";
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xD8 && static_cast<unsigned char>(b[2]) == 0xFF) return "jpg";
    if (b.starts_with("GIF87a") || b.starts_with("GIF89a")) return "gif";
    if (b.size() >= 12 && b.starts_with("RIFF") && b.substr(8, 4) == "WEBP") return "webp";
    return {};
}

void MessageStore::setPrefDir(const std::string& prefDir) {
    dir_ = prefDir + "chat";
    SDL_CreateDirectory(dir_.c_str());
    SDL_CreateDirectory((dir_ + "/media").c_str());
}

// ------------------------------------------------------------------------------------------------ sending

bool MessageStore::saveImage(const std::string& id, std::string_view bytes, std::string& fileName, std::string* error) const {
    const std::string ext = imageExtension(bytes);
    if (ext.empty()) {
        if (error) *error = "the picture must be a PNG, JPEG, GIF or WebP file";
        return false;
    }
    if (bytes.size() > kMaxImageBytes) {
        if (error) *error = "the picture is too big (8 MB at most)";
        return false;
    }
    fileName = id + "." + ext;
    if (fs::writeFile(mediaFile(fileName), bytes)) return true;
    if (error) *error = "cannot save the picture";
    return false;
}

bool MessageStore::put(const std::string& characterId, const Message& m, std::string* error) const {
    SDL_CreateDirectory(folder(characterId).c_str());
    cache_.erase(characterId);                                        // what we just wrote shows at once
    if (fs::writeFile(folder(characterId) + "/" + m.id + ".json", messageJson(m).dump(2) + "\n")) return true;
    if (error) *error = "cannot write the message";
    return false;
}

bool MessageStore::sendFromGm(std::span<const std::string> characterIds, bool broadcast, const std::string& to, const std::string& text,
                              const std::string& imagePath, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (dir_.empty()) return fail("no folder for the chat");
    if (characterIds.empty()) return fail("nobody to send it to");
    if (text.empty() && imagePath.empty()) return fail("nothing to send");
    if (text.size() > kMaxText) return fail("the text is too long");
    Message m;
    m.id = fs::stampedId("m");
    m.at = nowIso();
    m.from = "gm";
    m.kind = broadcast ? "broadcast" : "message";
    m.to = broadcast ? to : std::string();
    m.text = text;
    if (!imagePath.empty()) {
        const auto bytes = fs::readFile(imagePath);
        if (!bytes) return fail("cannot read the picture");
        if (!saveImage(m.id, *bytes, m.image, error)) return false;
    }
    for (const std::string& id : characterIds) {
        if (!fs::safeId(id) || id == "media") continue;
        if (!put(id, m, error)) return false;
        trim(id);
    }
    return true;
}

bool MessageStore::sendFromPlayer(const std::string& characterId, const std::string& text, std::string_view imageBytes, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (dir_.empty() || !fs::safeId(characterId) || characterId == "media") return fail("no conversation");
    if (text.empty() && imageBytes.empty()) return fail("nothing to send");
    if (text.size() > kMaxText) return fail("the text is too long");
    Message m;
    m.id = fs::stampedId("m");
    m.at = nowIso();
    m.from = "player";
    m.kind = "message";
    m.text = text;
    if (!imageBytes.empty() && !saveImage(m.id, imageBytes, m.image, error)) return false;
    if (!put(characterId, m, error)) return false;
    trim(characterId);
    return true;
}

// ------------------------------------------------------------------------------------------------- reading

const Thread& MessageStore::thread(const std::string& characterId) const {
    if (dir_.empty() || !fs::safeId(characterId) || characterId == "media") return kEmpty;
    Cached& slot = cache_[characterId];
    if (freshMs_ > 0 && slot.signature != 0 && SDL_GetTicks() - slot.checkedAt < freshMs_) return slot.thread;
    const size_t sig = signature(characterId);
    if (sig == 0) return kEmpty;
    slot.checkedAt = SDL_GetTicks();
    if (slot.signature == sig) return slot.thread;
    Thread t;
    for (const std::string& name : fs::listDir(folder(characterId))) {
        if (name == "_gm.read" || name == "_player.read") {
            const std::string marker = fs::readFile(folder(characterId) + "/" + name).value_or("");
            (name == "_gm.read" ? t.gmRead : t.playerRead) = marker;
        } else if (name.ends_with(".json")) {
            if (const auto j = jsonLoad(folder(characterId) + "/" + name)) t.messages.push_back(messageFrom(*j));
        }
    }
    std::erase_if(t.messages, [](const Message& m) { return m.id.empty(); });
    std::ranges::sort(t.messages, {}, &Message::id);
    slot.signature = sig;
    slot.thread = std::move(t);
    return slot.thread;
}

void MessageStore::markRead(const std::string& characterId, bool byGm, const std::string& upToId) {
    if (dir_.empty() || !fs::safeId(characterId) || !fs::safeId(upToId)) return;
    const std::string file = folder(characterId) + (byGm ? "/_gm.read" : "/_player.read");
    if (fs::readFile(file).value_or("") >= upToId) return;              // only ever forward
    fs::writeFile(file, upToId);
    cache_.erase(characterId);
}

int MessageStore::unread(const std::string& characterId, bool forGm) const {
    const Thread& t = thread(characterId);
    const std::string& marker = forGm ? t.gmRead : t.playerRead;
    const char* other = forGm ? "player" : "gm";
    return static_cast<int>(std::ranges::count_if(t.messages, [&](const Message& m) { return m.from == other && m.id > marker; }));
}

std::vector<std::string> MessageStore::poll() {
    std::vector<std::string> changed;
    if (dir_.empty()) return changed;
    std::map<std::string, size_t> now;
    for (const std::string& name : fs::listDir(dir_))
        if (name != "media" && fs::safeId(name))
            if (const size_t sig = signature(name)) now[name] = sig;
    for (const auto& [id, sig] : now)
        if (const auto known = signatures_.find(id); known == signatures_.end() || known->second != sig) changed.push_back(id);
    for (const auto& [id, sig] : signatures_)
        if (!now.contains(id)) changed.push_back(id);
    for (const std::string& id : changed) cache_.erase(id);
    signatures_ = std::move(now);
    const bool first = !primed_;
    primed_ = true;
    if (first) changed.clear();
    return changed;
}

// ---------------------------------------------------------------------------------------------- housekeeping

void MessageStore::trim(const std::string& characterId) const {
    std::vector<std::string> names;
    for (const std::string& n : fs::listDir(folder(characterId)))
        if (n.ends_with(".json")) names.push_back(n);
    if (names.size() <= kKeep) return;
    std::ranges::sort(names);
    for (size_t i = 0; i + kKeep < names.size(); ++i) SDL_RemovePath((folder(characterId) + "/" + names[i]).c_str());
    pruneMedia();
}

void MessageStore::forget(const std::string& characterId) {
    if (dir_.empty() || !fs::safeId(characterId) || characterId == "media") return;
    for (const std::string& n : fs::listDir(folder(characterId))) SDL_RemovePath((folder(characterId) + "/" + n).c_str());
    SDL_RemovePath(folder(characterId).c_str());
    cache_.erase(characterId);
    pruneMedia();
}

// Pictures that no conversation points to any more (trimmed away, or their character was deleted) are removed.
void MessageStore::pruneMedia() const {
    std::set<std::string> used;
    for (const std::string& who : fs::listDir(dir_)) {
        if (who == "media") continue;
        for (const std::string& n : fs::listDir(dir_ + "/" + who))
            if (n.ends_with(".json"))
                if (const auto j = jsonLoad(dir_ + "/" + who + "/" + n)) used.insert(jsonStr(*j, "image"));
    }
    for (const std::string& n : fs::listDir(dir_ + "/media"))
        if (!used.contains(n)) SDL_RemovePath((dir_ + "/media/" + n).c_str());
}

}  // namespace gm
