#include "web/web_backup.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <SDL3/SDL.h>

#include "parsing/fsutil.h"

namespace gm {
namespace {

constexpr const char* kFormat = "skaldbok-backup";
constexpr int kDepth = 8;                                            // folders inside folders: far more than the GM's folder has

// What belongs to this computer, at the top of the folder: its settings, the web status, the tokens of the links.
bool ofThisComputer(const std::string& name) {
    return name.starts_with("settings.") || name.starts_with("web-access.") || name.starts_with("web-status.");
}

bool isDir(const std::string& path) {
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

// "chat/c1/m-1.json": every part a plain name, never "..", a drive or a backslash.
bool plainPath(const std::string& path) {
    if (path.empty() || path.size() > 200) return false;
    size_t from = 0;
    int parts = 0;
    while (from <= path.size()) {
        const size_t slash = path.find('/', from);
        const std::string part = path.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
        if (part.empty() || part == "." || part == ".." || part.find_first_of("\\:*?\"<>|") != std::string::npos) return false;
        if (part.ends_with(".") || part.ends_with(" ") || part.ends_with(".tmp")) return false;
        if (++parts > kDepth) return false;
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return !ofThisComputer(path.substr(0, path.find('/')));
}

// Every file of the folder that a backup carries, as "relative/path".
void collect(const std::string& root, const std::string& rel, int depth, std::vector<std::string>& files, std::vector<std::string>& dirs) {
    for (const std::string& name : fs::listDir(root + (rel.empty() ? "" : "/" + rel))) {
        if (rel.empty() && ofThisComputer(name)) continue;
        if (name.ends_with(".tmp")) continue;
        const std::string path = rel.empty() ? name : rel + "/" + name;
        if (isDir(root + "/" + path)) {
            if (depth < kDepth) {
                dirs.push_back(path);
                collect(root, path, depth + 1, files, dirs);
            }
        } else {
            files.push_back(path);
        }
    }
}

}  // namespace

std::string base64Encode(std::string_view bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = static_cast<unsigned char>(bytes[i]);
        const unsigned b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        const unsigned n = (b0 << 16) | (b1 << 8) | b2;
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
    }
    return out;
}

std::optional<std::string> base64Decode(std::string_view in) {
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

json backupExport(const std::string& prefsDir, const std::string& now) {
    const std::string root = fs::withoutTrailingSlash(prefsDir);
    std::vector<std::string> files, dirs;
    collect(root, "", 0, files, dirs);
    std::ranges::sort(files);
    json list = json::object();
    for (const std::string& path : files) {
        const auto bytes = fs::readFile(root + "/" + path);
        if (bytes) list[path] = base64Encode(*bytes);
    }
    return {{"format", kFormat}, {"version", 1}, {"created", now}, {"files", list}};
}

BoardResult backupImport(const std::string& prefsDir, const json& backup) {
    BoardResult r;
    const auto fail = [&](const char* why) {
        r.status = 400;
        r.error = why;
        return r;
    };
    if (!backup.is_object() || jsonStr(backup, "format") != kFormat) return fail("That is not a Skaldbok backup.");
    const json* files = jsonFind(backup, "files");
    if (!files || !files->is_object()) return fail("The backup has no files.");
    // everything is checked before anything is touched
    std::map<std::string, std::string> decoded;
    for (const auto& [path, text] : files->items()) {
        if (!plainPath(path)) return fail("The backup has a file with a name that is not allowed.");
        if (!text.is_string()) return fail("The backup is damaged.");
        auto bytes = base64Decode(text.get<std::string>());
        if (!bytes) return fail("The backup is damaged.");
        decoded[path] = std::move(*bytes);
    }

    const std::string root = fs::withoutTrailingSlash(prefsDir);
    std::vector<std::string> old, dirs;
    collect(root, "", 0, old, dirs);
    for (const std::string& path : old) SDL_RemovePath((root + "/" + path).c_str());
    std::ranges::sort(dirs, [](const std::string& a, const std::string& b) { return a.size() > b.size(); });      // the deepest first
    for (const std::string& d : dirs) SDL_RemovePath((root + "/" + d).c_str());                                  // only the empty ones go

    int written = 0;
    for (const auto& [path, bytes] : decoded) {
        const size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) SDL_CreateDirectory((root + "/" + path.substr(0, slash)).c_str());
        if (fs::writeFile(root + "/" + path, bytes)) ++written;
    }
    if (written != static_cast<int>(decoded.size())) {
        r.status = 500;
        r.error = "Some files could not be written.";
        return r;
    }
    r.body = {{"files", written}};
    return r;
}

}  // namespace gm
