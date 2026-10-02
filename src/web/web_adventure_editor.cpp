#include "web/web_adventure_editor.h"

#include <algorithm>
#include <set>

#include <SDL3/SDL.h>

#include "game/messages.h"
#include "parsing/content.h"
#include "parsing/fsutil.h"
#include "parsing/fts.h"

namespace gm {
namespace {

constexpr int kMaxDepth = 8;
constexpr int kMaxNodes = 2000;
constexpr size_t kMaxBody = 60000;
constexpr size_t kMaxImage = 8u << 20;

const std::set<std::string> kKinds = {"chapter", "section", "location", "sidebar", "npc", "monster", "map", "text"};

std::string packsRoot(const std::string& prefsDir) { return fs::withoutTrailingSlash(prefsDir) + "/packs/"; }

std::string adventureDir(const std::string& prefsDir, const std::string& id) {
    if (!fs::safeId(id)) return std::string();
    const std::string dir = packsRoot(prefsDir) + id;
    return fs::isFile(dir + "/adventure.yaml") ? dir : std::string();
}

HomebrewResult failure(int status, const char* error) {
    HomebrewResult r;
    r.status = status;
    r.error = error;
    return r;
}

bool isDirectory(const std::string& path) {
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

void removeTree(const std::string& dir) {
    for (const std::string& name : fs::listDir(dir)) {
        const std::string path = dir + "/" + name;
        if (isDirectory(path)) removeTree(path);
        else SDL_RemovePath(path.c_str());
    }
    SDL_RemovePath(dir.c_str());
}

bool plainText(const json& v, size_t limit, std::string& out) {
    if (!v.is_string()) return false;
    out = v.get<std::string>();
    return out.size() <= limit;
}

// "images/uploads/x.png": a plain relative path to a picture inside the pack's images folder
bool pictureOk(const std::string& p) {
    const std::string lower = lowerCopy(p);
    return p.size() <= 200 && p.starts_with("images/") && p.find("..") == std::string::npos && p.find('\\') == std::string::npos && p.find("//") == std::string::npos &&
           (lower.ends_with(".png") || lower.ends_with(".jpg") || lower.ends_with(".jpeg") || lower.ends_with(".webp"));
}

// One node, cleaned; false when it is not one (the reason goes to `why`).
bool cleanNode(const json& in, int depth, int& count, std::set<std::string>& ids, json& out, std::string& why) {
    if (!in.is_object() || depth > kMaxDepth || ++count > kMaxNodes) {
        why = "The adventure is too big or deep.";
        return false;
    }
    std::string id, title, kind, body;
    if (!plainText(in.value("id", json()), 120, id) || id.empty() || !ids.insert(id).second) {
        why = "Every part of the adventure needs an id of its own.";
        return false;
    }
    if (!plainText(in.value("title", json()), 200, title)) {
        why = "A title is too long (200 letters at most).";
        return false;
    }
    if (!plainText(in.value("kind", json("section")), 20, kind) || !kKinds.count(kind)) {
        why = "A part has a kind that is not one of the adventure's.";
        return false;
    }
    out = {{"id", id}, {"title", trimmed(title)}, {"kind", kind}};
    if (const json* n = jsonFind(in, "number"); n && n->is_string() && !trimmed(n->get<std::string>()).empty() && n->get<std::string>().size() <= 20) out["number"] = trimmed(n->get<std::string>());
    if (const json* b = jsonFind(in, "body"); b && !b->is_null()) {
        if (!plainText(*b, kMaxBody, body)) {
            why = "A text is too long.";
            return false;
        }
        if (!trimmed(body).empty()) out["body"] = body;
    }
    for (const char* key : {"images", "creatures"}) {
        const json* list = jsonFind(in, key);
        if (!list || list->is_null()) continue;
        if (!list->is_array() || list->size() > 100) {
            why = "A list of pictures or creatures is not right.";
            return false;
        }
        json clean = json::array();
        for (const json& v : *list) {
            std::string s;
            if (!plainText(v, 200, s) || s.empty() || (std::string(key) == "images" && !pictureOk(s))) {
                why = "A picture or creature of the adventure is not right.";
                return false;
            }
            clean.push_back(s);
        }
        if (!clean.empty()) out[key] = clean;
    }
    if (const json* kids = jsonFind(in, "sections"); kids && !kids->is_null()) {
        if (!kids->is_array()) {
            why = "The parts of the adventure are not a list.";
            return false;
        }
        json list = json::array();
        for (const json& k : *kids) {
            json one;
            if (!cleanNode(k, depth + 1, count, ids, one, why)) return false;
            list.push_back(std::move(one));
        }
        if (!list.empty()) out["sections"] = list;
    }
    return true;
}

int countNodes(const json& nodes) {
    int n = 0;
    for (const json& node : nodes)
        if (node.is_object()) ++n;
    return n;
}

}  // namespace

json ownAdventures(const std::string& prefsDir) {
    json list = json::array();
    for (const std::string& id : fs::listDir(packsRoot(prefsDir))) {
        const std::string dir = adventureDir(prefsDir, id);
        if (dir.empty()) continue;
        const auto root = jsonLoad(dir + "/adventure.yaml");
        if (!root || !root->is_object()) continue;
        const json* chapters = jsonFind(*root, "chapters");
        list.push_back({{"id", id}, {"title", jsonStr(*root, "title", id)}, {"chapters", chapters && chapters->is_array() ? countNodes(*chapters) : 0}});
    }
    std::sort(list.begin(), list.end(), [](const json& a, const json& b) { return lowerCopy(a.value("title", "")) < lowerCopy(b.value("title", "")); });
    return {{"adventures", list}};
}

HomebrewResult createAdventure(const std::string& prefsDir, const std::string& title) {
    const std::string clean = trimmed(title);
    if (clean.empty() || clean.size() > 80) return failure(400, "Give the adventure a name (up to 80 letters).");
    std::string base = slugOf(clean);
    if (base.empty() || !fs::safeId(base)) return failure(400, "The name needs at least one letter or digit.");
    if (base.size() > 40) base.resize(40);
    std::string id = base;
    for (int n = 2; n < 1000 && (id == "core" || isDirectory(packsRoot(prefsDir) + id)); ++n) id = base + "-" + std::to_string(n);
    const std::string dir = packsRoot(prefsDir) + id;
    SDL_CreateDirectory(dir.c_str());
    const json manifest = {{"format", 1}, {"id", id}, {"name", clean}, {"version", "1"}, {"description", "An adventure made on the GM page."}};
    if (!fs::writeFile(dir + "/manifest.yaml", manifest.dump(2)) || !fs::writeFile(dir + "/adventure.yaml", json({{"title", clean}, {"chapters", json::array()}}).dump(2)))
        return failure(500, "Could not create the adventure.");
    HomebrewResult r;
    r.status = 201;
    r.body = {{"id", id}, {"title", clean}};
    return r;
}

json readAdventure(const std::string& prefsDir, const std::string& id) {
    const std::string dir = adventureDir(prefsDir, id);
    if (dir.empty()) return nullptr;
    const auto root = jsonLoad(dir + "/adventure.yaml");
    if (!root || !root->is_object()) return nullptr;
    const json* chapters = jsonFind(*root, "chapters");
    return {{"id", id}, {"title", jsonStr(*root, "title", id)}, {"chapters", chapters && chapters->is_array() ? *chapters : json::array()}};
}

HomebrewResult saveAdventure(const std::string& prefsDir, const std::string& id, const json& body) {
    const std::string dir = adventureDir(prefsDir, id);
    if (dir.empty()) return failure(404, "There is no such adventure.");
    if (!body.is_object()) return failure(400, "Send the adventure as JSON.");
    std::string title;
    if (!plainText(body.value("title", json()), 80, title) || trimmed(title).empty()) return failure(400, "Give the adventure a name (up to 80 letters).");
    const json* chapters = jsonFind(body, "chapters");
    if (!chapters || !chapters->is_array()) return failure(400, "The chapters must be a list.");
    json clean = json::array();
    std::set<std::string> ids;
    int count = 0;
    std::string why;
    for (const json& c : *chapters) {
        json one;
        if (!cleanNode(c, 1, count, ids, one, why)) return failure(400, why.c_str());
        clean.push_back(std::move(one));
    }
    json root = json::object();
    if (const auto old = jsonLoad(dir + "/adventure.yaml"); old && old->is_object()) root = *old;         // what else the file says stays
    root["title"] = trimmed(title);
    root["chapters"] = clean;
    if (!fs::writeFile(dir + "/adventure.yaml", root.dump(2))) return failure(500, "Could not save the adventure.");
    // the pack is called like the adventure
    if (auto manifest = jsonLoad(dir + "/manifest.yaml"); manifest && manifest->is_object() && jsonStr(*manifest, "name") != trimmed(title)) {
        (*manifest)["name"] = trimmed(title);
        fs::writeFile(dir + "/manifest.yaml", manifest->dump(2));
    }
    HomebrewResult r;
    r.body = {{"ok", true}};
    return r;
}

HomebrewResult deleteAdventure(const std::string& prefsDir, const std::string& id) {
    const std::string dir = adventureDir(prefsDir, id);
    if (dir.empty()) return failure(404, "There is no such adventure.");
    removeTree(dir);
    SDL_RemovePath((fs::withoutTrailingSlash(prefsDir) + "/adventure-notes/" + id + ".yaml").c_str());
    HomebrewResult r;
    r.body = ownAdventures(prefsDir);
    return r;
}

HomebrewResult saveAdventureImage(const std::string& prefsDir, const std::string& id, std::string_view bytes) {
    const std::string dir = adventureDir(prefsDir, id);
    if (dir.empty()) return failure(404, "There is no such adventure.");
    const std::string ext = MessageStore::imageExtension(bytes);
    if (ext != "png" && ext != "jpg" && ext != "webp") return failure(400, "The picture must be a PNG, JPEG or WebP file.");
    if (bytes.size() > kMaxImage) return failure(413, "The picture is too big (8 MB at most).");
    SDL_CreateDirectory((dir + "/images").c_str());
    SDL_CreateDirectory((dir + "/images/uploads").c_str());
    const std::string name = fs::stampedId("u") + "." + ext;
    if (!fs::writeFile(dir + "/images/uploads/" + name, bytes)) return failure(500, "Could not keep the picture.");
    HomebrewResult r;
    r.status = 201;
    r.body = {{"path", "images/uploads/" + name}};
    return r;
}

}  // namespace gm
