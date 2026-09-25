// The shape shared by the per-user stores (characters, parties): a folder with one "<id>.json" file per item, where the file
// name is the identity. T needs `id`, `name`, `createdAt`, `updatedAt`, `toJson()` and a static `fromJson(text, out, error)`.
//
// Another program (the web server) may write the same files, so the store can look again with poll() and picks up what changed.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "parsing/fsutil.h"

namespace gm {

template <class T>
class JsonDirStore {
public:
    virtual ~JsonDirStore() = default;

    void setDir(const std::string& dir) {
        dir_ = fs::withoutTrailingSlash(dir);
        if (!dir_.empty()) SDL_CreateDirectory(dir_.c_str());
        reload();
    }

    void reload() {
        list_.clear();
        stamps_.clear();
        if (!dir_.empty())
            for (const std::string& name : fs::listDir(dir_)) {
                if (!name.ends_with(".json")) continue;
                const std::string stem = name.substr(0, name.size() - 5);
                if (!fs::safeId(stem)) continue;
                T item;
                if (!load(stem, item)) continue;                            // a damaged file is skipped, the rest loads
                list_.push_back(std::move(item));
            }
        sortList();
        ++revision_;
    }

    // Looks at the folder again and reloads what another program added, changed or removed; returns those ids.
    std::vector<std::string> poll() {
        std::vector<std::string> changed;
        if (dir_.empty()) return changed;
        std::set<std::string> present;
        for (const std::string& name : fs::listDir(dir_)) {
            if (!name.ends_with(".json")) continue;
            const std::string stem = name.substr(0, name.size() - 5);
            if (!fs::safeId(stem)) continue;
            const auto text = fs::readFile(pathOf(stem));                   // these files are small, and the text is the one honest sign of change
            if (!text) continue;
            present.insert(stem);
            const Stamp stamp = stampOf(*text);
            if (const auto known = stamps_.find(stem); known != stamps_.end() && known->second == stamp) continue;
            T item;
            if (!T::fromJson(*text, item, nullptr)) continue;               // half written: it keeps what it had and is looked at again next time
            item.id = stem;
            stamps_[stem] = stamp;
            if (T* existing = find(stem)) *existing = std::move(item);
            else list_.push_back(std::move(item));
            changed.push_back(stem);
        }
        for (auto it = stamps_.begin(); it != stamps_.end();) {
            if (present.contains(it->first)) {
                ++it;
                continue;
            }
            std::erase_if(list_, [&](const T& item) { return item.id == it->first; });
            changed.push_back(it->first);
            it = stamps_.erase(it);
        }
        if (!changed.empty()) {
            sortList();
            ++revision_;
        }
        return changed;
    }

    std::vector<T>& all() { return list_; }
    const std::vector<T>& all() const { return list_; }
    T* find(const std::string& id) { return const_cast<T*>(std::as_const(*this).find(id)); }
    const T* find(const std::string& id) const {
        const auto it = std::ranges::find(list_, id, &T::id);
        return it == list_.end() ? nullptr : &*it;
    }

    std::string newId() const {
        SDL_Time t = 0;
        SDL_GetCurrentTime(&t);
        static std::mt19937 rng(static_cast<unsigned>(t) ^ 0x9E3779B9u);
        for (;;) {
            char buf[40];
            std::snprintf(buf, sizeof buf, "%c-%llx-%03x", prefix_, static_cast<unsigned long long>(t / 1000000), static_cast<unsigned>(rng() & 0xFFF));
            if (!find(buf)) return buf;
        }
    }

    // Adds or replaces (by id) and writes the file; updatedAt is set here.
    bool save(T& item, std::string* error = nullptr) {
        auto fail = [&](std::string why) {
            if (error) *error = std::move(why);
            return false;
        };
        if (dir_.empty()) return fail(std::string("no folder for ") + noun_ + "s");
        if (item.id.empty()) item.id = newId();
        if (!fs::safeId(item.id)) return fail(std::string("invalid ") + noun_ + " id");
        if (item.createdAt.empty()) item.createdAt = nowIso();
        item.updatedAt = nowIso();
        const T* memory = find(item.id);
        if (!commit(item, memory)) return fail(SDL_GetError());
        recordStamp(item.id);
        if (T* existing = find(item.id)) *existing = item;
        else list_.push_back(item);
        sortList();
        ++revision_;
        return true;
    }

    bool remove(const std::string& id) {
        if (!fs::safeId(id)) return false;
        const auto erased = std::erase_if(list_, [&](const T& item) { return item.id == id; });
        if (!erased) return false;
        SDL_RemovePath(pathOf(id).c_str());
        stamps_.erase(id);
        ++revision_;
        return true;
    }

    int revision() const { return revision_; }           // changes whenever the list does
    const std::string& dir() const { return dir_; }
    std::string pathOf(const std::string& id) const { return dir_ + "/" + id + ".json"; }

protected:
    JsonDirStore(char idPrefix, const char* noun) : prefix_(idPrefix), noun_(noun) {}

    // Writes `item` to its file. `memory` is what this store held for it, if anything. A store whose files are edited elsewhere at
    // the same time overrides this to fold the change into the file instead of replacing it (and may adjust `item` to match).
    virtual bool commit(T& item, const T* memory) {
        (void)memory;
        return fs::writeFile(pathOf(item.id), item.toJson());
    }

private:
    struct Stamp {
        size_t hash = 0, size = 0;
        bool operator==(const Stamp&) const = default;
    };

    static Stamp stampOf(const std::string& text) { return {std::hash<std::string>{}(text), text.size()}; }

    bool load(const std::string& id, T& out) {
        const auto text = fs::readFile(pathOf(id));
        if (!text || !T::fromJson(*text, out, nullptr)) return false;
        out.id = id;                                       // the file name is the identity
        stamps_[id] = stampOf(*text);
        return true;
    }

    void recordStamp(const std::string& id) {
        if (const auto text = fs::readFile(pathOf(id))) stamps_[id] = stampOf(*text);
    }

    void sortList() {
        auto lower = [](std::string s) {
            std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        std::ranges::sort(list_, [&](const T& a, const T& b) {
            const std::string la = lower(a.name), lb = lower(b.name);
            return la != lb ? la < lb : a.id < b.id;
        });
    }

    std::string dir_;
    std::vector<T> list_;
    std::map<std::string, Stamp> stamps_;
    int revision_ = 0;
    char prefix_;
    const char* noun_;
};

}  // namespace gm
