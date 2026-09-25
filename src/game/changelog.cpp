#include "game/changelog.h"

#include <algorithm>

#include <SDL3/SDL.h>

#include "game/character.h"
#include "parsing/fsutil.h"

namespace gm {
namespace {

json toJson(const ChangeEntry& e) {
    json j = {{"id", e.id}, {"at", e.at}, {"by", e.by}, {"summary", e.summary}, {"before", e.before}, {"after", e.after}};
    if (e.undone) j["undone"] = true;
    return j;
}

ChangeEntry entryFrom(const json& j) {
    ChangeEntry e;
    e.id = jsonStr(j, "id");
    e.at = jsonStr(j, "at");
    e.by = jsonStr(j, "by");
    e.summary = jsonStr(j, "summary");
    if (const json* b = jsonFind(j, "before")) e.before = *b;
    if (const json* a = jsonFind(j, "after")) e.after = *a;
    e.undone = jsonBool(j, "undone");
    return e;
}

std::vector<ChangeEntry> load(const std::string& path) {
    std::vector<ChangeEntry> out;
    if (const auto j = jsonLoad(path))
        if (const json* list = jsonFind(*j, "entries"); list && list->is_array())
            for (const json& e : *list)
                if (e.is_object() && !jsonStr(e, "id").empty()) out.push_back(entryFrom(e));
    return out;
}

}  // namespace

void ChangeLog::setPrefDir(const std::string& prefDir) {
    dir_ = prefDir + "changes";
    SDL_CreateDirectory(dir_.c_str());
}

void ChangeLog::save(const std::string& characterId, const std::vector<ChangeEntry>& list) const {
    json j = {{"format", 1}, {"entries", json::array()}};
    for (const ChangeEntry& e : list) j["entries"].push_back(toJson(e));
    fs::writeFile(fileFor(characterId), j.dump(2) + "\n");
}

void ChangeLog::add(const std::string& characterId, ChangeEntry entry) {
    if (dir_.empty() || !fs::safeId(characterId)) return;
    entry.id = fs::stampedId("x");
    entry.at = nowIso();
    std::vector<ChangeEntry> list = load(fileFor(characterId));
    list.push_back(std::move(entry));
    if (list.size() > kKeep) list.erase(list.begin(), list.end() - static_cast<std::ptrdiff_t>(kKeep));
    save(characterId, list);
}

std::vector<ChangeEntry> ChangeLog::entries(const std::string& characterId) const {
    return fs::safeId(characterId) && !dir_.empty() ? load(fileFor(characterId)) : std::vector<ChangeEntry>{};
}

bool ChangeLog::markUndone(const std::string& characterId, const std::string& entryId) {
    if (dir_.empty() || !fs::safeId(characterId)) return false;
    std::vector<ChangeEntry> list = load(fileFor(characterId));
    const auto it = std::ranges::find(list, entryId, &ChangeEntry::id);
    if (it == list.end() || it->undone) return false;
    it->undone = true;
    save(characterId, list);
    return true;
}

std::vector<std::pair<std::string, ChangeEntry>> ChangeLog::poll() {
    std::vector<std::pair<std::string, ChangeEntry>> fresh;
    if (dir_.empty()) return fresh;
    std::map<std::string, Stamp> now;
    for (const std::string& name : fs::listDir(dir_)) {
        if (!name.ends_with(".json")) continue;
        const std::string id = name.substr(0, name.size() - 5);
        SDL_PathInfo info{};
        if (fs::safeId(id) && SDL_GetPathInfo(fileFor(id).c_str(), &info)) now[id] = {static_cast<long long>(info.modify_time), info.size};
    }
    for (const auto& [id, stamp] : now) {
        if (const auto known = stamps_.find(id); known != stamps_.end() && known->second == stamp) continue;
        const std::vector<ChangeEntry> list = load(fileFor(id));
        std::string& newest = seen_[id];
        for (const ChangeEntry& e : list)
            if (primed_ && e.id > newest) fresh.emplace_back(id, e);
        if (!list.empty()) newest = std::max(newest, list.back().id);
    }
    stamps_ = std::move(now);
    primed_ = true;
    return fresh;
}

}  // namespace gm
