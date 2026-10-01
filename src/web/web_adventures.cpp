#include "web/web_adventures.h"

#include <algorithm>
#include <set>

#include <SDL3/SDL.h>

#include "parsing/fsutil.h"
#include "parsing/fts.h"

namespace gm {
namespace {

// The folder of the adventure with that id (the user's wins), or "".
std::string adventureDir(const std::string& dataDir, const std::string& prefsDir, const std::string& id) {
    if (!fs::safeId(id)) return std::string();
    for (const std::string& root : {fs::withoutTrailingSlash(prefsDir) + "/packs/", fs::withoutTrailingSlash(dataDir) + "/packs/"}) {
        const std::string dir = root + id;
        if (fs::isFile(dir + "/adventure.yaml")) return dir;
    }
    return std::string();
}

std::set<std::string> adventureIds(const std::string& dataDir, const std::string& prefsDir) {
    std::set<std::string> ids;
    for (const std::string& root : {fs::withoutTrailingSlash(prefsDir) + "/packs/", fs::withoutTrailingSlash(dataDir) + "/packs/"})
        for (const std::string& f : fs::listDir(root))
            if (fs::safeId(f) && fs::isFile(root + f + "/adventure.yaml")) ids.insert(f);
    return ids;
}

std::string packName(const std::string& dir, const std::string& fallback) {
    if (const auto m = jsonLoad(dir + "/manifest.yaml"); m && m->is_object() && !jsonStr(*m, "name").empty()) return jsonStr(*m, "name");
    return fallback;
}

int countNodes(const json& nodes) {
    int n = 0;
    for (const json& node : nodes)
        if (node.is_object()) ++n;
    return n;
}

// Every creature key and table name the nodes mention.
void collect(const json& nodes, std::set<std::string>& creatures, std::set<std::string>& tables) {
    for (const json& n : nodes) {
        if (!n.is_object()) continue;
        if (const json* c = jsonFind(n, "creatures"); c && c->is_array())
            for (const json& k : *c)
                if (k.is_string()) creatures.insert(k.get<std::string>());
        if (const std::string t = jsonStr(n, "table"); !t.empty()) tables.insert(t);
        if (const json* s = jsonFind(n, "sections"); s && s->is_array()) collect(*s, creatures, tables);
    }
}

}  // namespace

json adventureList(const std::string& dataDir, const std::string& prefsDir) {
    json list = json::array();
    for (const std::string& id : adventureIds(dataDir, prefsDir)) {
        const std::string dir = adventureDir(dataDir, prefsDir, id);
        const auto root = jsonLoad(dir + "/adventure.yaml");
        if (!root || !root->is_object()) continue;
        const json* chapters = jsonFind(*root, "chapters");
        const std::string title = jsonStr(*root, "title", id);
        list.push_back({{"id", id}, {"name", packName(dir, title)}, {"title", title}, {"chapters", chapters && chapters->is_array() ? countNodes(*chapters) : 0}});
    }
    std::sort(list.begin(), list.end(), [](const json& a, const json& b) { return lowerCopy(a.value("name", "")) < lowerCopy(b.value("name", "")); });
    return {{"adventures", list}};
}

json adventureDetail(const std::string& dataDir, const std::string& prefsDir, const std::string& id, const ContentStore& content) {
    const std::string dir = adventureDir(dataDir, prefsDir, id);
    if (dir.empty()) return nullptr;
    const auto root = jsonLoad(dir + "/adventure.yaml");
    if (!root || !root->is_object()) return nullptr;
    const json* chapters = jsonFind(*root, "chapters");
    json nodes = chapters && chapters->is_array() ? *chapters : json::array();

    std::set<std::string> creatureKeys, tableNames;
    collect(nodes, creatureKeys, tableNames);

    json tables = json::object();
    if (const auto t = jsonLoad(dir + "/tables.yaml"); t && t->is_object())
        if (const json* list = jsonFind(*t, "tables"); list && list->is_array())
            for (const json& table : *list) {
                const std::string name = table.is_object() ? jsonStr(table, "name") : std::string();
                if (name.empty() || !tableNames.count(name)) continue;
                json out = {{"title", name}, {"dice", jsonStr(table, "dice")}, {"columns", table.contains("columns") ? table["columns"] : json::array()}, {"rows", json::array()}};
                if (const json* rows = jsonFind(table, "rows"); rows && rows->is_array())
                    for (const json& r : *rows) out["rows"].push_back({{"roll", r.is_object() ? jsonStr(r, "roll") : std::string()}, {"cells", r.is_object() && r.contains("cells") ? r["cells"] : json::array()}});
                tables[name] = out;
            }

    json creatures = json::object();
    for (const Monster& m : content.monsters())
        if (creatureKeys.count(m.key)) creatures[m.key] = {{"name", m.name}, {"kind", m.kind}};

    const std::string title = jsonStr(*root, "title", id);
    return {{"id", id}, {"name", packName(dir, title)}, {"title", title}, {"chapters", nodes}, {"tables", tables}, {"creatures", creatures}, {"notes", adventureNotes(prefsDir, id)}};
}

std::string adventureImagePath(const std::string& dataDir, const std::string& prefsDir, const std::string& id, const std::string& relative) {
    const std::string dir = adventureDir(dataDir, prefsDir, id);
    const std::string lower = lowerCopy(relative);
    const bool picture = lower.ends_with(".png") || lower.ends_with(".jpg") || lower.ends_with(".jpeg") || lower.ends_with(".webp");
    if (dir.empty() || !picture || !relative.starts_with("images/") || relative.find("..") != std::string::npos || relative.find('\\') != std::string::npos || relative.find("//") != std::string::npos)
        return std::string();
    const std::string path = dir + "/" + relative;
    return fs::isFile(path) ? path : std::string();
}

namespace {
std::string notesFile(const std::string& prefsDir, const std::string& id) { return fs::withoutTrailingSlash(prefsDir) + "/adventure-notes/" + id + ".yaml"; }
}  // namespace

json adventureNotes(const std::string& prefsDir, const std::string& id) {
    json notes = json::object();
    if (!fs::safeId(id)) return notes;
    if (const auto root = jsonLoad(notesFile(prefsDir, id)); root && root->is_object())
        if (const json* n = jsonFind(*root, "notes"); n && n->is_object())
            for (auto it = n->begin(); it != n->end(); ++it)
                if (it.value().is_string()) notes[it.key()] = it.value();
    return notes;
}

NoteResult saveAdventureNote(const std::string& dataDir, const std::string& prefsDir, const std::string& id, const std::string& node, const std::string& text) {
    NoteResult r;
    if (adventureDir(dataDir, prefsDir, id).empty()) {
        r.status = 404;
        r.error = "There is no such adventure.";
        return r;
    }
    if (node.empty() || node.size() > 200) {
        r.status = 400;
        r.error = "Say which part of the adventure the note is for.";
        return r;
    }
    if (text.size() > 20000) {
        r.status = 400;
        r.error = "The note is too long (20000 letters at most).";
        return r;
    }
    json notes = adventureNotes(prefsDir, id);
    if (trimmed(text).empty()) notes.erase(node);
    else notes[node] = text;
    SDL_CreateDirectory((fs::withoutTrailingSlash(prefsDir) + "/adventure-notes").c_str());
    if (!fs::writeFile(notesFile(prefsDir, id), json({{"notes", notes}}).dump(2))) {
        r.status = 500;
        r.error = "Could not save the note.";
    }
    return r;
}

}  // namespace gm
