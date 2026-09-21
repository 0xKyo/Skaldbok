#include "packs.h"

#include <algorithm>
#include <cctype>

#include <SDL3/SDL.h>
#include <miniz.h>

#include "fts.h"

namespace gm {
namespace {

constexpr size_t kMaxZipBytes = 256u << 20;
constexpr unsigned long long kMaxUnpackedBytes = 512ull << 20;
constexpr size_t kMaxFileBytes = 96u << 20;
constexpr int kMaxFiles = 5000;

std::string norm(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string extensionOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : lowerCopy(name.substr(dot + 1));
}

// What a pack may contain: data and pictures. Anything else (scripts, executables) is left behind.
bool allowedFile(const std::string& name) {
    static const char* const ok[] = {"json", "png", "jpg", "jpeg", "gif", "bmp", "txt", "md"};
    const std::string e = extensionOf(name);
    for (const char* x : ok)
        if (e == x) return true;
    return false;
}

bool pathInfo(const std::string& p, SDL_PathInfo& info) { return SDL_GetPathInfo(p.c_str(), &info); }
bool isDir(const std::string& p) {
    SDL_PathInfo i;
    return pathInfo(p, i) && i.type == SDL_PATHTYPE_DIRECTORY;
}
bool isFileAt(const std::string& p) {
    SDL_PathInfo i;
    return pathInfo(p, i) && i.type == SDL_PATHTYPE_FILE;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> out;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<std::vector<std::string>*>(user)->push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &out);
    std::sort(out.begin(), out.end());
    return out;
}

struct CopyState {
    int files = 0;
    unsigned long long bytes = 0;
    bool tooBig = false;
};

void copyTree(const std::string& from, const std::string& to, CopyState& st) {
    SDL_CreateDirectory(to.c_str());
    for (const std::string& name : listDir(from)) {
        if (name.empty() || name[0] == '.') continue;
        const std::string src = from + "/" + name, dst = to + "/" + name;
        SDL_PathInfo info;
        if (!pathInfo(src, info)) continue;
        if (info.type == SDL_PATHTYPE_DIRECTORY) {
            copyTree(src, dst, st);
        } else if (info.type == SDL_PATHTYPE_FILE && allowedFile(name)) {
            if (++st.files > kMaxFiles || (st.bytes += info.size) > kMaxUnpackedBytes || info.size > kMaxFileBytes) {
                st.tooBig = true;
                return;
            }
            SDL_CopyFile(src.c_str(), dst.c_str());
        }
        if (st.tooBig) return;
    }
}

// The folder that holds manifest.json: `dir` itself, or its only subfolder ("MyPack.zip" often holds "MyPack/").
std::string findPackRoot(const std::string& dir) {
    if (isFileAt(dir + "/manifest.json")) return dir;
    std::string found;
    for (const std::string& name : listDir(dir)) {
        if (name.empty() || name[0] == '.' || name == "__MACOSX") continue;
        if (isDir(dir + "/" + name) && isFileAt(dir + "/" + name + "/manifest.json")) {
            if (!found.empty()) return {};
            found = dir + "/" + name;
        }
    }
    return found;
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) SDL_CreateDirectory(path.substr(0, slash).c_str());
    return SDL_SaveFile(path.c_str(), data, size);
}

// Unpacks a .zip into `dest`. Entries that would land outside it (zip-slip) or that are not data/pictures are skipped.
bool unzip(const std::string& zipPath, const std::string& dest, std::string* error) {
    SDL_PathInfo info;
    if (!pathInfo(zipPath, info) || info.type != SDL_PATHTYPE_FILE) {
        *error = "file not found";
        return false;
    }
    if (info.size > kMaxZipBytes) {
        *error = "the zip file is too large";
        return false;
    }
    size_t size = 0;
    void* data = SDL_LoadFile(zipPath.c_str(), &size);
    if (!data) {
        *error = SDL_GetError();
        return false;
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
        SDL_free(data);
        *error = "this is not a valid .zip file";
        return false;
    }
    bool ok = true;
    unsigned long long total = 0;
    int files = 0;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n && ok; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        const std::string name = norm(st.m_filename);
        if (!isSafeArchivePath(name)) {
            *error = "the zip contains an unsafe path (\"" + name + "\")";
            ok = false;
            break;
        }
        if (!allowedFile(name) || name.contains("/.") || name[0] == '.') continue;
        if (st.m_is_encrypted || !st.m_is_supported) continue;
        if (++files > kMaxFiles || st.m_uncomp_size > kMaxFileBytes || (total += st.m_uncomp_size) > kMaxUnpackedBytes) {
            *error = "the zip is too big to be a content pack";
            ok = false;
            break;
        }
        size_t outSize = 0;
        void* buf = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
        if (!buf) {
            *error = "could not read \"" + name + "\" from the zip";
            ok = false;
            break;
        }
        ok = writeFile(dest + "/" + name, buf, outSize);
        if (!ok) *error = "could not write " + name;
        mz_free(buf);
    }
    mz_zip_reader_end(&zip);
    SDL_free(data);
    return ok;
}

}  // namespace

bool isSafeArchivePath(const std::string& name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.contains(':')) return false;
    size_t pos = 0;
    while (pos <= name.size()) {
        size_t end = name.find_first_of("/\\", pos);
        if (end == std::string::npos) end = name.size();
        if (name.substr(pos, end - pos) == "..") return false;
        pos = end + 1;
    }
    return true;
}

bool removeTree(const std::string& dir) {
    SDL_PathInfo info;
    if (!pathInfo(dir, info)) return true;
    if (info.type == SDL_PATHTYPE_DIRECTORY)
        for (const std::string& name : listDir(dir)) removeTree(dir + "/" + name);
    return SDL_RemovePath(dir.c_str());
}

PackManager::PackManager(std::string coreDir, std::string userDir) : coreDir_(norm(std::move(coreDir))), userDir_(norm(std::move(userDir))) {}

std::vector<PackSpec> PackManager::specs(const std::set<std::string>& disabled) const {
    std::vector<PackSpec> out;
    if (!coreDir_.empty()) out.push_back({coreDir_, true, true});
    if (userDir_.empty()) return out;
    for (const std::string& name : listDir(userDir_)) {
        if (name.empty() || name[0] == '.') continue;
        const std::string dir = userDir_ + "/" + name;
        if (isDir(dir) && isFileAt(dir + "/manifest.json")) out.push_back({dir, false, disabled.count(name) == 0});
    }
    return out;
}

ImportResult PackManager::import(const std::string& rawPath, Database& db) {
    ImportResult r;
    const std::string path = norm(rawPath);
    if (userDir_.empty()) {
        r.message = "There is no folder to keep imported packs in.";
        return r;
    }
    SDL_CreateDirectory(userDir_.c_str());
    // leftovers of an interrupted import
    for (const std::string& name : listDir(userDir_))
        if (name.starts_with(".stage") || name.starts_with(".old")) removeTree(userDir_ + "/" + name);

    const std::string stage = userDir_ + "/.stage";
    std::string err;
    SDL_PathInfo info;
    if (!pathInfo(path, info)) {
        r.message = "\"" + path + "\" does not exist.";
        return r;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        CopyState st;
        copyTree(path, stage, st);
        if (st.tooBig) {
            removeTree(stage);
            r.message = "That folder is too big to be a content pack.";
            return r;
        }
    } else if (extensionOf(path) == "zip") {
        if (!unzip(path, stage, &err)) {
            removeTree(stage);
            r.message = "Could not open the zip: " + err;
            return r;
        }
    } else {
        r.message = "Choose a pack folder or a .zip file.";
        return r;
    }

    const std::string root = findPackRoot(stage);
    if (root.empty()) {
        removeTree(stage);
        r.message = "No manifest.json found. A pack is a folder with a manifest.json and the type files next to it.";
        return r;
    }
    PackInfo probe;
    if (!readManifest(root, probe)) {
        removeTree(stage);
        r.message = "Not a valid pack: " + probe.error;
        return r;
    }
    if (probe.id == "core") {
        removeTree(stage);
        r.message = "The id \"core\" is reserved for the built-in content. Choose another id in manifest.json.";
        return r;
    }
    // load it once, on its own, exactly as the app will
    ContentStore trial;
    trial.load(db, {PackSpec{root, false, true}});
    const PackInfo& loaded = trial.packs().front();
    if (!loaded.loaded) {
        removeTree(stage);
        r.message = "The pack could not be loaded: " + loaded.error;
        return r;
    }
    r.packId = loaded.id;
    r.packName = loaded.name;
    r.warnings = loaded.warnings;
    r.entries = loaded.total();

    const std::string dest = userDir_ + "/" + loaded.id, old = userDir_ + "/.old";
    r.replaced = isDir(dest);
    if (r.replaced) SDL_RenamePath(dest.c_str(), old.c_str());
    if (!SDL_RenamePath(root.c_str(), dest.c_str())) {
        if (r.replaced) SDL_RenamePath(old.c_str(), dest.c_str());
        removeTree(stage);
        r.message = std::string("Could not install the pack: ") + SDL_GetError();
        return r;
    }
    removeTree(stage);
    removeTree(old);
    r.ok = true;
    r.message = std::string(r.replaced ? "Updated " : "Installed ") + "\"" + loaded.name + "\" (" + std::to_string(r.entries) + " entries" +
                (r.warnings.empty() ? ")" : ", " + std::to_string(r.warnings.size()) + " warning(s))");
    return r;
}

bool PackManager::remove(const std::string& packId, std::string* error) {
    if (packId.empty() || packId == "core" || !isSafeArchivePath(packId) || packId.contains('/')) {
        if (error) *error = "That pack cannot be removed.";
        return false;
    }
    const std::string dir = userDir_ + "/" + packId;
    if (!isDir(dir)) {
        if (error) *error = "That pack is not installed.";
        return false;
    }
    if (!removeTree(dir)) {
        if (error) *error = std::string("Could not delete the folder: ") + SDL_GetError();
        return false;
    }
    return true;
}

}  // namespace gm
