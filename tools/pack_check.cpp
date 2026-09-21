// pack_check: validates a homebrew content pack (a folder or a .zip) exactly the way the app does when importing it.
//
//   pack_check <folder-or-zip> [--data <folder with dragonbane.db>] [--strict]
//
// Prints what the pack contains and every problem found. Exit code 0 = the app will import it, 1 = it will refuse it,
// 2 = it imports but has warnings and --strict was given. Nothing is installed anywhere.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "content.h"
#include "packs.h"

namespace {

std::string normalize(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

bool fileExists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string findData(const std::string& given) {
    std::vector<std::string> candidates;
    if (!given.empty()) candidates.push_back(given);
    if (const char* env = std::getenv("SKALDBOK_DATA")) candidates.push_back(env);
    if (const char* base = SDL_GetBasePath()) {
        const std::string b = normalize(base);
        for (const char* rel : {"/data", "/../data", "/../../data", "/../../../data"}) candidates.push_back(b + rel);
    }
#ifdef SKALDBOK_DEV_DATA_DIR
    candidates.push_back(SKALDBOK_DEV_DATA_DIR);
#endif
    for (std::string c : candidates) {
        c = normalize(c);
        if (fileExists(c + "/dragonbane.db")) return c;
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    std::string path, dataArg;
    bool strict = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataArg = argv[++i];
        else if (a == "--strict") strict = true;
        else if (path.empty()) path = a;
    }
    if (path.empty()) {
        std::printf("usage: pack_check <folder-or-zip> [--data <folder with dragonbane.db>] [--strict]\n");
        return 64;
    }
    const std::string data = findData(dataArg);
    if (data.empty()) {
        std::printf("cannot find data/dragonbane.db (use --data <folder>)\n");
        return 64;
    }
    gm::Database db;
    std::string err;
    if (!db.open(data + "/dragonbane.db", &err)) {
        std::printf("cannot open the database: %s\n", err.c_str());
        return 64;
    }

    // import into a throw-away folder: that is the app's own code path
    std::string tmp = SDL_GetBasePath() ? normalize(SDL_GetBasePath()) : std::string(".");
    tmp += "/pack-check-tmp";
    gm::removeTree(tmp);
    gm::PackManager pm(data + "/packs/core", tmp);
    const gm::ImportResult r = pm.import(path, db);
    if (!r.ok) {
        std::printf("REFUSED: %s\n", r.message.c_str());
        gm::removeTree(tmp);
        return 1;
    }
    gm::ContentStore store;
    store.load(db, pm.specs({}));
    if (const gm::PackInfo* p = store.pack(r.packId)) {
        std::printf("OK  %s (id \"%s\")%s%s\n", p->name.c_str(), p->id.c_str(), p->version.empty() ? "" : "  v", p->version.c_str());
        static const struct {
            gm::Kind kind;
            const char* name;
        } names[] = {{gm::Kind::Monster, "creatures"}, {gm::Kind::Spell, "spells"},   {gm::Kind::Ability, "abilities"}, {gm::Kind::Skill, "skills"},
                     {gm::Kind::Kin, "kin"},           {gm::Kind::Profession, "professions"}, {gm::Kind::Weapon, "weapons"}, {gm::Kind::Armor, "armor"},
                     {gm::Kind::Gear, "gear items"},   {gm::Kind::Table, "tables"}};
        for (const auto& n : names)
            if (p->counts[static_cast<int>(n.kind)] > 0) std::printf("    %3d %s\n", p->counts[static_cast<int>(n.kind)], n.name);
        for (int sid : p->sourceIds)
            if (const gm::SourceInfo* s = store.source(sid)) std::printf("    source: %s\n", s->label.c_str());
        for (const std::string& w : p->warnings) std::printf("  warning: %s\n", w.c_str());
        gm::removeTree(tmp);
        if (!p->warnings.empty() && strict) return 2;
        return 0;
    }
    gm::removeTree(tmp);
    std::printf("REFUSED: the pack disappeared after installing\n");
    return 1;
}
