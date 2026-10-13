#include "parsing/content.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iterator>

#include <SDL3/SDL.h>
#include <sqlite3.h>

#include "parsing/fsutil.h"
#include "parsing/fts.h"
#include "parsing/i18n.h"
#include "parsing/jsonutil.h"
#include "parsing/sql.h"

namespace gm {
namespace {

constexpr int kFormat = 1;                    // the pack format this build understands
constexpr size_t kMaxFileBytes = 64u << 20;   // a pack file bigger than this is not a pack file
constexpr size_t kMaxWarnings = 60;

bool validPackId(const std::string& id) {
    if (id.empty() || id.size() > 64 || !(std::islower(static_cast<unsigned char>(id[0])) || std::isdigit(static_cast<unsigned char>(id[0]))))
        return false;
    for (unsigned char c : id)
        if (!(std::islower(c) || std::isdigit(c) || c == '-' || c == '_')) return false;
    return true;
}

// A pack may only point at files inside its own folder.
bool safeRelPath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p[0] == '\\' || p.contains(':')) return false;
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t end = p.find_first_of("/\\", pos);
        if (end == std::string::npos) end = p.size();
        if (p.substr(pos, end - pos) == "..") return false;
        pos = end + 1;
    }
    return true;
}

bool isFile(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool readTextFile(const std::string& path, std::string& out, std::string* error) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
        if (error) *error = "file not found";
        return false;
    }
    if (info.size > kMaxFileBytes) {
        if (error) *error = "file is too large";
        return false;
    }
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        if (error) *error = SDL_GetError();
        return false;
    }
    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
    return true;
}

// "1", "1-2", "1–2", "3 to 4": the rolls a table row covers.
bool parseRoll(const std::string& text, int& lo, int& hi) {
    int nums[2] = {0, 0};
    int n = 0;
    for (size_t i = 0; i < text.size() && n < 2;) {
        if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            size_t j = i;
            int v = 0;
            while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) v = v * 10 + (text[j++] - '0');
            nums[n++] = v;
            i = j;
        } else {
            ++i;
        }
    }
    if (n == 0) return false;
    lo = nums[0];
    hi = n == 2 ? nums[1] : nums[0];
    if (hi < lo) std::swap(lo, hi);
    return true;
}

std::string normalizeDice(std::string d) {
    d = trimmed(d);
    for (char& c : d) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (d.size() > 2 && d[0] == '1' && d[1] == 'D') d.erase(0, 1);
    return d;
}

unsigned colorFor(const std::string& text) {
    unsigned h = 2166136261u;
    for (unsigned char c : text) h = (h ^ c) * 16777619u;
    const float hue = static_cast<float>(h % 360u) / 60.0f, s = 0.55f, v = 0.95f;
    const int i = static_cast<int>(hue);
    const float f = hue - static_cast<float>(i), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r = v, g = t, b = p;
    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
    }
    return (static_cast<unsigned>(r * 255) << 16) | (static_cast<unsigned>(g * 255) << 8) | static_cast<unsigned>(b * 255);
}

void addField(std::vector<Field>& out, const std::string& label, const std::string& value) {
    if (!value.empty()) out.push_back({label, value});
}

std::string joinList(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : sep) + s;
    return out;
}

// What a pack says about itself (besides its data): these keys, in manifest.json or at the top of a data file.
bool hasHeaderKeys(const json& o) {
    for (const char* k : {"id", "name", "version", "author", "description", "sources"})
        if (o.contains(k)) return true;
    return false;
}

std::string folderNameOf(std::string dir) {
    for (char& c : dir)
        if (c == '\\') c = '/';
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    const size_t slash = dir.find_last_of('/');
    return slash == std::string::npos ? dir : dir.substr(slash + 1);
}

}  // namespace

std::string slugOf(const std::string& s) {
    std::string out;
    bool dash = false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            if (dash && !out.empty()) out += '-';
            out += static_cast<char>(std::tolower(c));
            dash = false;
        } else {
            dash = true;
        }
    }
    return out;
}

// ------------------------------------------------------------------------------------- manifest

// The kinds' data files in the order a pack loads them (after them, rules.json: see Loader::load). A new kind goes here too.
// (the skills come first: a spell names its school, a profession its skills, by id)
static constexpr Kind kLoadOrder[] = {Kind::Skill,  Kind::Spell, Kind::Ability, Kind::Kin,   Kind::Profession,
                                      Kind::Weapon, Kind::Armor, Kind::Gear,    Kind::Table, Kind::Monster};

const std::vector<std::string>& packDataFiles() {
    static const std::vector<std::string> files = [] {
        std::vector<std::string> f = {"rules", "world", "actions", "equipment"};
        for (Kind k : kLoadOrder) f.push_back(kindFile(k));
        return f;
    }();
    return files;
}

std::string systemDirOf(const std::string& coreDir) {
    std::string d = coreDir;
    for (char& c : d)
        if (c == '\\') c = '/';
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    for (int up = 0; up < 2; ++up) {                              // packs/core -> packs -> data
        const size_t slash = d.find_last_of('/');
        if (slash == std::string::npos) return std::string();
        d.erase(slash);
    }
    return d + "/system";
}

bool looksLikePack(const std::string& dir) {
    if (isFile(dir + "/manifest.yaml")) return true;
    for (const std::string& f : packDataFiles())
        if (isFile(dir + "/" + f + ".yaml")) return true;
    return false;
}

// A pack describes itself in its manifest.json or, when it has none, in the header at the top of the first of its data files that has
// one ({"name": "...", "sources": [...], "rules": [...]}); its id is then the name of its folder. `meta` gets that description, in
// `lang` (a translation may rename the pack and point its books at the PDFs of that language).
static bool readMeta(const std::string& dir, PackInfo& out, json& meta, const std::string& lang = i18n::kDefault) {
    out.dir = dir;
    meta = json::object();
    std::string text, err;
    const bool hasManifest = isFile(dir + "/manifest.yaml");
    if (hasManifest) {
        if (!readTextFile(dir + "/manifest.yaml", text, &err)) {
            out.error = "manifest.yaml: " + err;
            return false;
        }
        if (!jsonParse(text, meta, &err)) {
            out.error = "manifest.yaml: " + err;
            return false;
        }
        if (!meta.is_object()) {
            out.error = "manifest.yaml must be a YAML mapping";
            return false;
        }
        const std::string id = jsonStr(meta, "id");                     // a translation never changes what the pack is
        if (const auto overlay = i18n::overlayFor(dir, "manifest.yaml", lang)) i18n::mergeOverlay(meta, *overlay);
        if (!id.empty()) meta["id"] = id;
    } else {
        bool anyFile = false;
        for (const std::string& f : packDataFiles()) {
            const std::string path = dir + "/" + f + ".yaml";
            if (!isFile(path)) continue;
            anyFile = true;
            json j;
            if (!readTextFile(path, text, &err) || !jsonParse(text, j, &err)) {
                out.error = f + ".yaml: " + err;
                return false;
            }
            if (j.is_object() && hasHeaderKeys(j)) {
                meta = j;
                break;
            }
        }
        if (!anyFile) {
            out.error = "manifest.yaml: not found, and the folder has no data file either (rules.yaml, spells.yaml...)";
            return false;
        }
    }
    if (jsonInt(meta, "format", kFormat) > kFormat) {
        out.error = "made for a newer version of the app (pack format " + std::to_string(jsonInt(meta, "format")) + ")";
        return false;
    }
    out.id = jsonStr(meta, "id");
    if (out.id.empty() && !hasManifest) out.id = folderNameOf(dir);
    if (!validPackId(out.id)) {
        out.error = hasManifest ? "manifest.yaml: \"id\" must be lowercase letters, digits, - or _ (like \"my-tome\")"
                                : "manifest.yaml: there is none, and the folder name \"" + out.id + "\" is not a valid pack id (lowercase letters, digits, - or _)";
        return false;
    }
    out.name = jsonStr(meta, "name", out.id);
    out.version = jsonStr(meta, "version");
    out.author = jsonStr(meta, "author");
    out.description = jsonStr(meta, "description");
    return true;
}

bool readManifest(const std::string& dir, PackInfo& out) {
    json meta;
    return readMeta(dir, out, meta);
}

// ------------------------------------------------------------------------------------- loading

static std::string shownValue(const std::string& format, const std::string& value);

struct ContentStore::Loader {
    ContentStore& st;
    PackInfo& pk;
    const json manifest;
    std::string sysDir;                 // Core only: the game system's own text (data/system), see systemDirOf()
    std::string lang;                   // the language the content is read in (i18n.h): its translations are laid over the English
    std::map<std::string, int> srcByKey;
    int firstSource = 0;
    std::set<std::string> usedIds[kKindCount];
    std::set<std::string> usedRuleIds;

    Loader(ContentStore& store, PackInfo& pack, json man, std::string system, std::string language)
        : st(store), pk(pack), manifest(std::move(man)), sysDir(std::move(system)), lang(std::move(language)) {}

    void warn(const std::string& msg) {
        if (pk.warnings.size() < kMaxWarnings) pk.warnings.push_back(msg);
        else if (pk.warnings.size() == kMaxWarnings) pk.warnings.push_back("(more problems not listed)");
    }

    // A word of the program's own (a field's label, a subtitle) in the language of the content.
    std::string T(const std::string& english) const { return i18n::tr(lang, english); }

    // Parses a data file's text and lays its translation over it (<dir>/i18n/<lang>/<file>, i18n.h). A broken translation is a warning:
    // the file is then read in English.
    bool parseLocalized(const std::string& dir, const std::string& file, const std::string& text, json& root, std::string& err) {
        if (!jsonParse(text, root, &err)) return false;
        std::string overlayError;
        if (const auto overlay = i18n::overlayFor(dir, file, lang, &overlayError)) i18n::mergeOverlay(root, *overlay);
        else if (!overlayError.empty()) warn(overlayError);
        // the words a file keeps in the localization files (`name: LOC_SKILL_BUSHCRAFT`): the text of each key, in this language
        i18n::resolveTexts(root, [&](const std::string& key) { return st.text(key); }, [&](const std::string& key) { return st.englishText(key); }, lang != i18n::kDefault);
        return true;
    }

    // ---- attributes and lookups (game/attributes.h) ------------------------------------------------

    // An attribute as a file writes it, as ids: a string ("strength", an old code "STR", or several, "STR/AGL") or a list of them. A
    // name this system has no attribute for stays as written (the checks that need an attribute then do not find one).
    std::vector<std::string> attributeIds(const json& v) const {
        std::vector<std::string> words, ids;
        if (v.is_array()) {
            for (const json& x : v) words.push_back(trimmed(jsonText(x)));
        } else {
            const std::string text = trimmed(jsonText(v));
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('/', start);
                if (end == std::string::npos) end = text.size();
                words.push_back(trimmed(text.substr(start, end - start)));
                start = end + 1;
            }
        }
        for (const std::string& w : words) {
            if (w.empty()) continue;
            const std::string id = st.attributes_.idOf(w);
            ids.push_back(id.empty() ? w : id);
        }
        return ids;
    }

    // Their short forms on a card ("STR/AGL", "FUE/AGI").
    std::string shownAttributes(const std::vector<std::string>& ids) const {
        std::string out;
        for (const std::string& id : ids) out += (out.empty() ? "" : "/") + st.attributeAbbr(id);
        return out;
    }

    // attributes.yaml and lookups.yaml, in data/system (Core) and in the pack itself: the attributes of the game system, and the numeric tables the
    // rules read. Each file's tables are made here, with their words taken from the localization files.
    bool attributeFiles() {
        std::vector<std::string> dirs;
        if (!sysDir.empty()) dirs.push_back(sysDir);
        dirs.push_back(pk.dir);
        for (const std::string& dir : dirs)
            for (const char* file : {"attributes.yaml", "lookups.yaml"}) {
                const std::string path = dir + "/" + file;
                if (!isFile(path)) continue;
                std::string text, err;
                json root;
                if (!readTextFile(path, text, &err) || !jsonParse(text, root, &err)) {
                    pk.error = std::string(file) + ": " + err;
                    return false;
                }
                if (!root.is_object()) {
                    pk.error = std::string(file) + " must be a YAML mapping";
                    return false;
                }
                std::vector<std::string> problems;
                if (std::string(file) == "attributes.yaml") {
                    const json* list = jsonFind(root, "attributes");
                    if (list) st.attributes_.addAttributes(*list, problems);
                    if (const json* t = jsonFind(root, "table"); t && t->is_object() && list && list->is_array()) {
                        std::vector<std::string> ids;
                        for (const json& a : *list) ids.push_back(trimmed(jsonStr(a, "id")));
                        attributesTable(*t, ids);
                    }
                } else if (const json* list = jsonFind(root, "lookups")) {
                    st.attributes_.addLookups(*list, problems);
                    if (list->is_array())
                        for (const json& l : *list)
                            if (const Lookup* lookup = st.attributes_.lookup(trimmed(jsonStr(l, "id")))) lookupTable(*lookup);
                }
                for (const std::string& p : problems) warn(std::string(file) + ": " + p);
            }
        return true;
    }

    // A table of the rules with a title and header keys: its words come from the localization files, and the English title is what
    // "{{table: Attributes}}" and the table's key know it by.
    void generatedTable(const std::string& titleKey, const std::vector<std::string>& columns, const json& rows) {
        const std::string title = st.text(titleKey), english = st.englishText(titleKey);
        json cols = json::array();
        for (const std::string& c : columns) cols.push_back(st.text(c));
        json t = {{"name", title}, {"columns", cols}, {"rows", rows}};
        if (english != title) t["_key"] = english;
        table(t, title);
    }

    void attributesTable(const json& spec, const std::vector<std::string>& ids) {
        json rows = json::array();
        for (const std::string& id : ids)
            if (const AttributeDef* a = st.attributes_.find(id); a && a->id == id)
                rows.push_back({{"cells", json::array({st.text(a->nameKey), st.text(a->abbrKey), st.text(a->descriptionKey)})}});
        if (const std::string title = trimmed(jsonStr(spec, "title")); !title.empty()) generatedTable(title, jsonStrings(spec, "columns"), rows);
    }

    void lookupTable(const Lookup& l) {
        if (l.titleKey.empty()) return;
        json rows = json::array();
        for (const LookupRow& r : l.rows) {
            std::string label = r.labelKey.empty() ? std::string() : st.text(r.labelKey);
            if (label.empty()) {                                                // no text for the range: "from–to", "n+", "≤n"
                if (r.hasFrom && r.hasTo) label = std::to_string(r.from) + "\xE2\x80\x93" + std::to_string(r.to);
                else if (r.hasFrom) label = std::to_string(r.from) + "+";
                else if (r.hasTo) label = "\xE2\x89\xA4" + std::to_string(r.to);
            }
            rows.push_back({{"cells", json::array({label, shownValue(l.format, r.value)})}});
        }
        generatedTable(l.titleKey, {l.rangeKey, l.valueKey}, rows);
    }

    // What a translated item was called in English (its "_key", see i18n::mergeOverlay), or its name.
    static std::string englishName(const json& o, const std::string& name) {
        const std::string en = trimmed(jsonStr(o, "_key"));
        return en.empty() ? name : en;
    }

    void addSource(const std::string& key, const std::string& title, const std::string& label, bool homebrew) {
        SourceInfo s;
        s.id = st.sources_.empty() ? 1 : st.sources_.back().id + 1;
        if (homebrew) s.id = std::max(s.id, 100);
        s.key = key;
        s.packId = pk.id;
        s.title = title;
        s.label = label;
        s.homebrew = homebrew;
        s.color = colorFor(pk.id + key);
        st.sources_.push_back(s);
        srcByKey[key] = s.id;
        pk.sourceIds.push_back(s.id);
        if (!firstSource) firstSource = s.id;
    }

    // A book: a source with its PDF, its page count and the number printed on each page ("printed_pages", one per physical page).
    void addBook(const json& o) {
        SourceInfo s;
        s.id = st.sources_.empty() ? 1 : st.sources_.back().id + 1;
        s.key = jsonStr(o, "key");
        s.packId = pk.id;
        s.title = jsonStr(o, "title", s.key);
        s.label = jsonStr(o, "short", s.title);
        s.book = true;
        s.color = s.key == "rulebook" ? 0x4DC7B3u : s.key == "bestiary" ? 0xEE9E40u : s.key == "adventure" ? 0xB38CF2u : colorFor(s.key);
        s.file = jsonStr(o, "file");
        s.pages = jsonInt(o, "pages");
        if (const json* printed = jsonFind(o, "printed_pages"); printed && printed->is_array()) {
            // every page's number, written out
            for (const json& n : *printed) s.printedPages.push_back(n.is_number_integer() ? n.get<int>() : 0);
        } else if (const int offset = jsonInt(o, "page_offset"); offset > 0) {
            // the usual case: the number printed on a page is its PDF page minus a fixed offset, from the first numbered page on
            // ("first_numbered_page", by default the one after the offset), except the pages listed in "unnumbered_pages"
            const int first = std::max(1, jsonInt(o, "first_numbered_page", offset + 1));
            std::set<int> skip;
            if (const json* un = jsonFind(o, "unnumbered_pages"); un && un->is_array())
                for (const json& n : *un)
                    if (n.is_number_integer()) skip.insert(n.get<int>());
            for (int p = 1; p <= s.pages; ++p) s.printedPages.push_back(p >= first && !skip.count(p) ? p - offset : 0);
        }
        s.pages = std::max(s.pages, static_cast<int>(s.printedPages.size()));
        st.sources_.push_back(std::move(s));
    }

    void setupSources() {
        const json* list = jsonFind(manifest, "sources");
        if (pk.core) {
            // The books belong to the built-in packs: the first one that declares a book (with its PDF) creates it, the next ones
            // only rename its badge. Every built-in pack can then refer to any book.
            if (list && list->is_array())
                for (const json& o : *list) {
                    const std::string key = jsonStr(o, "key"), shortName = jsonStr(o, "short");
                    if (key.empty()) continue;
                    SourceInfo* have = nullptr;
                    for (SourceInfo& s : st.sources_)
                        if (s.book && s.key == key) have = &s;
                    if (have) {
                        if (!shortName.empty()) have->label = shortName;
                    } else if (!jsonStr(o, "file").empty()) {
                        addBook(o);
                    }
                }
            for (const SourceInfo& s : st.sources_)
                if (s.book) {
                    srcByKey[s.key] = s.id;
                    pk.sourceIds.push_back(s.id);
                    if (!firstSource) firstSource = s.id;
                }
            return;
        }
        if (list && list->is_array())
            for (const json& o : *list) {
                const std::string key = jsonStr(o, "key");
                if (key.empty() || srcByKey.count(key)) continue;
                const std::string title = jsonStr(o, "title", jsonStr(o, "short", key));
                addSource(key, title, T("Homebrew") + " · " + jsonStr(o, "short", title), true);
            }
        if (srcByKey.empty()) addSource(pk.id, pk.name, T("Homebrew") + " · " + pk.name, true);
    }

    int sourceFor(const json& o, const std::string& what) {
        const std::string key = jsonStr(o, "source");
        if (key.empty()) return firstSource;
        auto it = srcByKey.find(key);
        if (it != srcByKey.end()) return it->second;
        warn(what + ": unknown source \"" + key + "\" (using the pack's first source)");
        return firstSource;
    }

    // "<pack>/<kind>/<id>"; ids are unique inside a pack and kind. Made of the "id", else of the English name (a translation keeps
    // the keys of the English content).
    // An id is "<what it is>_<name in camelCase>" ("skills_bluffing", "spell_cookFood"); the key keeps the words of the name apart by "-"
    // ("cook-food"), which is what the keys always were. An id without the "_" (a homebrew's own) is slugged as it is.
    static std::string keyPartOfId(const std::string& id) {
        const size_t us = id.find('_');
        if (us == std::string::npos) return slugOf(id);
        std::string out;
        for (size_t i = us + 1; i < id.size(); ++i) {
            const char ch = id[i];
            if (ch >= 'A' && ch <= 'Z' && i > us + 1) out += '-';           // (a "_" later on is a "-" too: "skeletons_2")
            out += ch;
        }
        return slugOf(out);
    }

    // The name of what an id names (as shown, or in English); anything that is no id is a name already and stays as it is.
    std::string shownName(const std::string& ref, bool english = false) const {
        const auto it = st.idNames_.find(ref);
        return it == st.idNames_.end() ? ref : english ? it->second.second : it->second.first;
    }

    // `english`: the name of the English card (what a bane is: the sheet's rules know skills by it), else as shown
    std::vector<std::string> references(const json& o, const char* key, bool english = false) {
        std::vector<std::string> out = jsonStrings(o, key);
        for (std::string& s : out) s = shownName(s, english);
        return out;
    }

    std::string keyIn(const char* kind, std::set<std::string>& used, const json& o, const std::string& name) {
        std::string id = keyPartOfId(jsonStr(o, "id"));
        if (id.empty()) id = slugOf(englishName(o, name));
        if (id.empty()) id = "entry";
        std::string cand = id;
        for (int n = 2; !used.insert(cand).second; ++n) cand = id + "-" + std::to_string(n);
        if (cand != id && !jsonStr(o, "id").empty()) warn(std::string(kind) + " \"" + name + "\": id \"" + id + "\" is used twice");
        return pk.id + "/" + kind + "/" + cand;
    }

    std::string makeKey(Kind k, const json& o, const std::string& name) { return keyIn(kindKey(k), usedIds[static_cast<int>(k)], o, name); }

    void pageInfo(const json& o, int sourceId, PageRef& ref, std::string& note) {
        const int page = jsonInt(o, "page");
        const SourceInfo* si = st.source(sourceId);
        if (si && si->book && page > 0) {
            ref.sourceId = sourceId;
            ref.page = page;                                       // the physical page of the PDF: the one thing an entry says about its page
            ref.printed = st.printedPage(sourceId, page);          // the number printed on it comes from the book's header
        } else if (page > 0) {
            note = T("p.") + std::to_string(page);                 // a source without a PDF: the page is only shown
        }
    }

    // Extra display fields any card may carry: "fields": {"Label": "value"}
    void extraFields(const json& o, std::vector<Field>& out) {
        const json* f = jsonFind(o, "fields");
        if (!f) return;
        if (f->is_object()) {
            for (auto it = f->begin(); it != f->end(); ++it) addField(out, it.key(), jsonText(it.value()));
        } else if (f->is_array()) {
            for (const json& e : *f)
                if (e.is_object()) addField(out, jsonStr(e, "label"), jsonStr(e, "value"));
                else if (e.is_array() && e.size() >= 2) addField(out, jsonText(e[0]), jsonText(e[1]));
        }
    }

    // Pure data to a view. Data files describe things (entities), not tables: {"name", "kind", "category", "roll", "fields": {...}}.
    // Every entity of a category is one row of that category's table: the views (and the rules' "{{table: Category}}" lines) draw them
    // as a table, so a table is only ever a way of showing entities. The name is the first column, headed by its "kind" ("hazard" ->
    // HAZARD); "fields" are the other columns, headed by their keys. A "roll" ("3", "4-5") gives the entity when the die is rolled; the die
    // is the highest roll of the category (or an entity's own "dice"). The entities' kind is also the table's role ("weakness").
    // A file of entities may leave out their "category": it is then the name of the file ("weakness.yaml" -> "Weakness", "improvised-weapons.yaml" ->
    // "Improvised Weapons"). `fallback` is that name; "" where there is no file to take it from (an intro, a card), so the category is required.
    static std::string categoryFromFile(const std::string& file) {
        std::string stem = file.ends_with(".yaml") ? file.substr(0, file.size() - 5) : file, out;
        bool word = true;
        for (char ch : stem) {
            if (ch == '-' || ch == '_') {
                out += ' ';
                word = true;
            } else {
                out += word ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
                word = false;
            }
        }
        return out;
    }

    // `defaults`: the file-level "kind", "category" and "source" of a file of entities, which every entity that does not write its own takes
    // (so a file whose entities all share them says it once).
    // A card's own entities are named after the card: `cardName` as shown, `cardEnglish` in English (what "rollable" names).
    // The rules in the "fields" of an entity: "<name>_amount" with "<name>_dice" (an id of a die, or "-" for no die) is one column, <NAME> in the language
    // ("rations_amount: 2" + "rations_dice: core_dice_D6" says "2D6"; an amount of 1 with a die says just "D6"; with no die, the number).
    json expandFields(const json& fields) {
        json out = json::object();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const std::string& key = it.key();
            const bool amount = key.ends_with("_amount") && fields.contains(key.substr(0, key.size() - 7) + "_dice");
            if (key.ends_with("_dice") && fields.contains(key.substr(0, key.size() - 5) + "_amount")) continue;      // (said with its amount)
            if (!amount) {
                out[key] = it.value();
                continue;
            }
            const std::string base = key.substr(0, key.size() - 7);
            std::string upper;
            for (char c : base) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const std::string label = st.text("LOC_LABEL_" + upper);
            const std::string n = jsonText(it.value());
            const std::string die = shownName(trimmed(jsonStr(fields, (base + "_dice").c_str())));
            out[label == "LOC_LABEL_" + upper ? base : label] = die.empty() || die == "-" ? n : (n == "1" ? die : n + die);
        }
        return out;
    }

    json tablesFromEntities(const json& source, const std::string& where, const std::set<std::string>& rollable = {}, const std::string& fallback = std::string(),
                            const json& defaults = json::object(), const std::string& cardName = std::string(), const std::string& cardEnglish = std::string()) {
        const auto field = [&](const json& e, const char* key) {
            const std::string own = trimmed(jsonStr(e, key));
            return own.empty() ? trimmed(jsonStr(defaults, key)) : own;
        };
        // the English category, whatever a translation calls it: what the table's key is made of (see i18n::mergeOverlay)
        const auto englishOf = [&](const json& o, const std::string& otherwise) {
            if (const json* en = jsonFind(o, "_category_key")) {
                const std::string t = trimmed(jsonText(*en));
                return t.empty() ? otherwise : t;
            }
            const std::string own = trimmed(jsonStr(o, "category"));
            return own.empty() ? otherwise : own;
        };
        const std::string defaultEnglish = englishOf(defaults, fallback);
        json tables = json::array();
        if (!source.is_array()) return tables;
        json list = source;                                           // (its entities, with the rules of their fields said in words)
        for (json& e : list)
            if (const json* f = jsonFind(e, "fields"); f && f->is_object()) e["fields"] = expandFields(*f);
        std::vector<std::string> order;
        std::map<std::string, std::vector<const json*>> groups;
        std::map<std::string, std::string> english;         // category -> its English
        int index = 0;
        for (const json& e : list) {
            ++index;
            const std::string name = e.is_object() ? trimmed(jsonText(e.contains("name") ? e["name"] : json(""))) : std::string();
            std::string category = e.is_object() ? field(e, "category") : std::string();
            std::string categoryEnglish;
            if (category.empty() && !cardName.empty() && e.is_object()) {        // a card's own entity: "<card>: <Kind>" ("Human: First Name")
                std::string kind = field(e, "kind");
                bool word = true;
                for (char& ch : kind) {
                    if (ch == '-' || ch == '_') {
                        ch = ' ';
                        word = true;
                    } else {
                        if (word) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                        word = false;
                    }
                }
                if (!kind.empty()) {
                    category = cardName + ": " + T(kind);
                    categoryEnglish = (cardEnglish.empty() ? cardName : cardEnglish) + ": " + kind;
                }
            }
            if (category.empty()) category = fallback;
            if (name.empty() || category.empty()) {
                warn(where + ": entity #" + std::to_string(index) + " needs a \"name\" and a \"category\" (skipped)");
                continue;
            }
            if (!groups.count(category)) {
                order.push_back(category);
                english[category] = !categoryEnglish.empty() ? categoryEnglish : englishOf(e, defaultEnglish);
            }
            groups[category].push_back(&e);
        }
        for (const std::string& category : order) {
            const std::vector<const json*>& members = groups[category];
            std::string kind = field(*members.front(), "kind"), dice, source;
            std::vector<std::string> keys;
            int highest = 0;
            bool described = false;                         // an entity with a "description": a DESCRIPTION column right after the name
            for (const json* e : members) {
                described |= !jsonStr(*e, "description").empty();
                if (const json* f = jsonFind(*e, "fields"); f && f->is_object())
                    for (auto it = f->begin(); it != f->end(); ++it)
                        if (std::find(keys.begin(), keys.end(), it.key()) == keys.end()) keys.push_back(it.key());
                int lo = 0, hi = 0;
                if (const std::string roll = jsonStr(*e, "roll"); !roll.empty() && parseRoll(roll, lo, hi)) highest = std::max(highest, hi);
                if (dice.empty()) dice = jsonStr(*e, "dice");
                if (source.empty()) source = field(*e, "source");
            }
            if (dice.empty() && highest > 0) dice = "D" + std::to_string(highest);
            // a rollable category needs no rolls written: its entities are the numbers 1..N in file order, and the die is as many-sided as they are
            if (dice.empty() && (rollable.count(category) || rollable.count(english[category]))) dice = "D" + std::to_string(members.size());
            std::string head = kind.empty() ? "NAME" : kind;
            for (char& c : head) c = c == '-' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            json columns = json::array({T(head)});
            if (described) columns.push_back(T("DESCRIPTION"));
            for (const std::string& key : keys) columns.push_back(key);
            json rows = json::array();
            for (const json* e : members) {
                json cells = json::array({trimmed(jsonText((*e)["name"]))});
                if (described) cells.push_back(trimmed(jsonStr(*e, "description")));
                const json* f = jsonFind(*e, "fields");
                for (const std::string& key : keys) {
                    const json* v = f && f->is_object() ? jsonFind(*f, key.c_str()) : nullptr;
                    cells.push_back(v ? jsonText(*v) : std::string());
                }
                json row = {{"cells", cells}};
                if (const std::string roll = jsonStr(*e, "roll"); !roll.empty()) row["roll"] = roll;
                rows.push_back(row);
            }
            json table = {{"name", category}, {"columns", columns}, {"rows", rows}};
            if (english[category] != category) table["_key"] = english[category];
            if (!dice.empty()) table["dice"] = dice;
            if (!kind.empty()) table["role"] = kind;
            if (!source.empty()) table["source"] = source;
            tables.push_back(std::move(table));
        }
        return tables;
    }

    // The tables an object writes: its "tables" (the plain form, still accepted) and the ones made from its "entities".
    json tablesOf(const json& o, const std::string& where, const std::string& fallbackCategory = std::string(), const json& defaults = json::object(),
                  const std::string& cardName = std::string(), const std::string& cardEnglish = std::string()) {
        json all = json::array();
        std::set<std::string> rollable;                    // "rollable": the categories the reader can roll on (a random one of them)
        if (const json* r = jsonFind(o, "rollable"); r && r->is_array())
            for (const json& x : *r) rollable.insert(trimmed(jsonText(x)));
        if (const json* t = jsonFind(o, "tables"); t && t->is_array())
            for (const json& x : *t) all.push_back(x);
        if (const json* ents = jsonFind(o, "entities"))
            for (json& x : tablesFromEntities(*ents, where, rollable, fallbackCategory, defaults, cardName, cardEnglish)) all.push_back(std::move(x));
        return all;
    }

    // "tables"/"entities": tables that belong to the card (a kin's first names), shown when it is opened
    void fillCardTables(Kind k, const json& o, const std::string& name, Entry& e) {
        e.tables.clear();          // a replaced card starts over: any table it had is gone unless this JSON writes its own
        const json tables = tablesOf(o, std::string(kindKey(k)) + " \"" + name + "\"", std::string(), json::object(), name, englishName(o, name));
        int index = 0;
        for (const json& t : tables) {
            ++index;
            const std::string tableName = t.is_object() ? trimmed(jsonStr(t, "name")) : std::string();
            if (tableName.empty()) {
                warn(std::string(kindKey(k)) + " \"" + name + "\": table #" + std::to_string(index) + " has no \"name\", skipped");
                continue;
            }
            DataTable table;
            table.key = e.key + "#" + std::to_string(index);
            table.title = tableName;
            table.canon = englishName(t, tableName);
            table.sourceId = jsonStr(t, "source").empty() ? e.sourceId : sourceFor(t, "table \"" + tableName + "\"");
            fillTable(t, table);
            table.browse = false;
            e.tables.push_back(std::move(table));
        }
    }

    Entry& push(Kind k, const json& o, const std::string& name) {
        // "replaces" (a full "<pack>/<kind>/<id>" key, as "see" uses): a homebrew card can override one from an earlier pack
        // in place, exactly like a rule can - same id and key, its own text and picture, "Changed by <pack>" on the card.
        if (const std::string target = jsonStr(o, "replaces"); !target.empty()) {
            const auto& byKey = st.byKey_[static_cast<int>(k)];
            const auto found = byKey.find(target);
            if (found == byKey.end()) {
                warn(std::string(kindKey(k)) + " \"" + name + "\": replaces \"" + target + "\", which is not loaded (skipped)");
            } else {
                Entry& e = st.entries_[static_cast<int>(k)][static_cast<size_t>(found->second) - 1];
                e.title = name;
                e.canon = englishName(o, name);
                e.sourceId = sourceFor(o, std::string(kindKey(k)) + " \"" + name + "\"");
                pageInfo(o, e.sourceId, e.ref, e.pageNote);
                if (const std::string image = jsonStr(o, "image"); !image.empty()) e.image = pk.dir + "/" + image;
                // the caller (kin(), profession()...) rebuilds these from scratch next, exactly as it would for a new card
                e.fields.clear();
                e.props.clear();
                e.lists.clear();
                fillCardTables(k, o, name, e);
                e.editedBy = pk.name;
                ++pk.counts[static_cast<int>(k)];
                return e;
            }
        }
        Entry e;
        e.kind = k;
        e.key = makeKey(k, o, name);
        e.sourceId = sourceFor(o, std::string(kindKey(k)) + " \"" + name + "\"");
        e.title = name;
        e.canon = englishName(o, name);
        pageInfo(o, e.sourceId, e.ref, e.pageNote);
        if (const std::string image = jsonStr(o, "image"); !image.empty()) e.image = pk.dir + "/" + image;
        fillCardTables(k, o, name, e);
        auto& v = st.entries_[static_cast<int>(k)];
        e.id = static_cast<int>(v.size()) + 1;
        st.byKey_[static_cast<int>(k)][e.key] = e.id;
        if (const std::string own = trimmed(jsonStr(o, "id")); !own.empty()) {
            st.idNames_[own] = {name, e.canon};
            st.idKeys_[own] = e.key;
        }
        v.push_back(std::move(e));
        ++pk.counts[static_cast<int>(k)];
        return v.back();
    }

    // Reads <file>.json and calls fn for every named object of its array. Core reads it from data/system first (its intro, its rules:
    // what is not an item of the category) and then from the pack folder, which holds the items, like any other pack.
    // `listKey`: the top-level key holding the list when it is not the file's own name (equipment.yaml holds the weapons, the armor and
    // the gear under "weapons", "armor" and "gear"); a file that lacks that list is fine then.
    bool eachObject(const std::string& file, const std::function<void(const json&, const std::string&)>& fn, const std::string& listKey = std::string()) {
        if (!sysDir.empty() && !eachObjectIn(sysDir, file, fn, true, listKey)) return false;
        return eachObjectIn(pk.dir, file, fn, listKey.empty() ? false : true, listKey);
    }

    // `listOptional`: a system file may hold only an intro.
    bool eachObjectIn(const std::string& dir, const std::string& file, const std::function<void(const json&, const std::string&)>& fn, bool listOptional, const std::string& listKey = std::string()) {
        const std::string path = dir + "/" + file + ".yaml";
        if (!isFile(path)) return true;
        std::string text, err;
        if (!readTextFile(path, text, &err)) {
            pk.error = file + ".yaml: " + err;
            return false;
        }
        json root;
        if (!parseLocalized(dir, file + ".yaml", text, root, err)) {
            pk.error = file + ".yaml: " + err;
            return false;
        }
        if (jsonInt(root, "format", kFormat) > kFormat) {
            pk.error = file + ".yaml: made for a newer version of the app";
            return false;
        }
        // A data file's own general text, its page's "Intro" tab (docs/HOMEBREW.md): {"intro": "...", "<file>": [...]}, or with its
        // own named sections like a rule: {"intro": {"body": "...", "sections": [{"name","body"}]}, ...}. A later pack's non-empty
        // intro for the same kind replaces an earlier one, like a rule's "replaces".
        Kind introKind;
        if (root.is_object() && kindFromFile(file, introKind) && introKind != Kind::Table)
                if (const json* iv = jsonFind(root, "intro")) {
                    Intro in;
                    if (iv->is_string()) in.body = trimmed(iv->get<std::string>());
                    else if (iv->is_object()) {
                        in.body = trimmed(jsonStr(*iv, "body"));
                        if (const json* secs = jsonFind(*iv, "sections"); secs && secs->is_array())
                            for (const json& sec : *secs) {
                                const std::string secName = trimmed(jsonStr(sec, "name"));
                                if (secName.empty()) continue;
                                RuleNode::Section s;
                                s.title = secName;
                                s.canon = englishName(sec, secName);
                                if (const json* b = jsonFind(sec, "body")) s.body = trimmed(jsonText(*b));
                                in.sections.push_back(std::move(s));
                            }
                        {
                            const json tabs = tablesOf(*iv, file + ".yaml intro");
                            int index = 0;
                            for (const json& t : tabs) {
                                ++index;
                                const std::string tableName = t.is_object() ? trimmed(jsonStr(t, "name")) : std::string();
                                if (tableName.empty()) {
                                    warn(file + ".yaml intro: table #" + std::to_string(index) + " has no \"name\", skipped");
                                    continue;
                                }
                                DataTable table;
                                table.key = "intro/" + file + "#" + std::to_string(index);
                                table.title = tableName;
                                table.canon = englishName(t, tableName);
                                table.sourceId = sourceFor(t, file + ".yaml intro, table \"" + tableName + "\"");
                                fillTable(t, table);
                                table.browse = false;
                                in.tables.push_back(std::move(table));
                            }
                        }
                    }
                    if (!in.empty()) {
                        in.file = path;
                        st.intros_[static_cast<int>(introKind)] = std::move(in);
                    }
                }
        const std::string key = listKey.empty() ? file : listKey;
        const json* arr = root.is_array() && listKey.empty() ? &root : jsonFind(root, key.c_str());
        if (!arr && listOptional) return true;
        if (!arr || !arr->is_array()) {
            pk.error = file + ".yaml: expected a list (either the whole file or under \"" + key + "\")";
            return false;
        }
        size_t index = 0;
        for (const json& o : *arr) {
            ++index;
            if (!o.is_object()) {
                warn(file + ".yaml #" + std::to_string(index) + ": not an object, skipped");
                continue;
            }
            const std::string name = trimmed(jsonStr(o, "name"));
            if (name.empty()) {
                warn(file + ".yaml #" + std::to_string(index) + ": has no \"name\", skipped");
                continue;
            }
            fn(o, name);
        }
        return true;
    }

    // ---- card kinds -------------------------------------------------------------------------------

    void spell(const json& o, const std::string& name) {
        Entry& e = push(Kind::Spell, o, name);
        const std::string school = jsonStr(o, "school").empty() ? T("General Magic") : shownName(jsonStr(o, "school"));   // (a school is a skill, named by id)
        const bool trick = jsonBool(o, "trick");
        e.subtitle = school + " · " + (trick ? T("magic trick") : T("spell"));
        addField(e.fields, T("Rank"), jsonStr(o, "rank"));
        // what a spell requires of the caster names cards (a school, other spells): its words are made once every card is loaded (resolveLinks)
        if (const json* pre = jsonFind(o, "prerequisite")) {
            addField(e.fields, T("Prerequisite"), pre->is_string() ? pre->get<std::string>() : std::string("…"));
            e.props["prerequisite.ref"] = pre->dump();
        }
        addField(e.fields, T("Requirement"), jsonStr(o, "requirement"));
        addField(e.fields, T("Casting time"), jsonStr(o, "casting_time"));
        addField(e.fields, T("Range"), jsonStr(o, "range"));
        addField(e.fields, T("Duration"), jsonStr(o, "duration"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["school"] = school;
        e.props["trick"] = trick ? "1" : "0";
        e.props["rank"] = jsonStr(o, "rank");
    }

    void ability(const json& o, const std::string& name) {
        Entry& e = push(Kind::Ability, o, name);
        std::string type = lowerCopy(jsonStr(o, "type", "heroic"));
        if (type != "kin") type = "heroic";
        const std::string kin = jsonStr(o, "kin");               // a kin by id: its name, and the subtitle that says it, come with resolveLinks
        e.subtitle = type == "heroic" ? T("Heroic ability") : T("Innate ability");
        // the requirement is a skill (or several) by id, and its rank apart; its words are made once every card is loaded (resolveLinks)
        if (const json* req = jsonFind(o, "requirement")) {
            addField(e.fields, T("Requirement"), req->is_string() ? req->get<std::string>() : std::string("…"));
            e.props["requirement.ref"] = req->dump();
            e.props["requirement.rank"] = jsonStr(o, "rank");
        }
        addField(e.fields, T("Willpower points"), jsonStr(o, "wp_cost"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["type"] = type;
        e.props["kin"] = kin;
        e.props["wp_cost"] = jsonStr(o, "wp_cost");
    }

    void skill(const json& o, const std::string& name) {
        Entry& e = push(Kind::Skill, o, name);
        std::string category = lowerCopy(jsonStr(o, "category", "core"));
        // the attribute (an id, an old code, or several: a list, or "STR/AGL"): what the card stores is always the id
        const std::vector<std::string> ids = attributeIds(o.contains("attribute") ? o["attribute"] : json(""));
        e.subtitle = T(category + " skill");
        addField(e.fields, T("Attribute"), shownAttributes(ids));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["attribute"] = ids.size() == 1 ? ids.front() : std::string();       // a skill of several attributes has none of its own
        e.lists["attributes"] = ids;
        e.props["category"] = category;
    }

    void kin(const json& o, const std::string& name) {
        Entry& e = push(Kind::Kin, o, name);
        e.subtitle = T("Kin");
        const int movement = jsonInt(o, "movement");
        addField(e.fields, T("Movement"), movement > 0 ? std::to_string(movement) : "");
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
        e.props["description"] = e.body;              // as written: the body later gets its innate abilities' texts appended (resolveLinks)
        e.props["movement"] = movement > 0 ? std::to_string(movement) : "";
        e.lists["innate_abilities"] = references(o, "innate_abilities");
        e.lists["names"] = jsonStrings(o, "names");
        // a kin written with its table of first names does not repeat them in a list: the first column of the first table is the list
        if (e.lists["names"].empty() && !e.tables.empty())
            for (const TableRow& r : e.tables.front().rows)
                if (!r.cells.empty()) e.lists["names"].push_back(r.cells.front());
    }

    void profession(const json& o, const std::string& name) {
        Entry& e = push(Kind::Profession, o, name);
        e.subtitle = T("Profession");
        const std::vector<std::string> keyIds = attributeIds(o.contains("key_attribute") ? o["key_attribute"] : json(""));
        addField(e.fields, T("Key attribute"), shownAttributes(keyIds));
        e.props["key_attribute"] = keyIds.size() == 1 ? keyIds.front() : std::string();
        e.lists["skills"] = references(o, "skills");
        if (!e.list("skills").empty()) addField(e.fields, T("Skills"), joinList(e.list("skills"), ", "));
        if (const json* bs = jsonFind(o, "skills_by_school"); bs && bs->is_object()) {
            std::vector<std::string> schools;
            for (auto it = bs->begin(); it != bs->end(); ++it) {
                schools.push_back(it.key());
                e.lists["skills." + it.key()] = jsonStrings(*bs, it.key().c_str());
                addField(e.fields, T(it.key() + " skills"), joinList(e.lists["skills." + it.key()], ", "));
            }
            e.lists["schools"] = schools;
        }
        std::vector<std::string> heroic = references(o, "heroic_abilities");
        const std::string orWord = " " + T("or") + " ";
        if (heroic.empty()) {                       // "heroic_ability": "A, B or C" as printed
            std::string one = jsonStr(o, "heroic_ability");
            size_t pos;
            while ((pos = one.find(" or ")) != std::string::npos) one.replace(pos, 4, ", ");
            while (orWord != " or " && (pos = one.find(orWord)) != std::string::npos) one.replace(pos, orWord.size(), ", ");
            for (size_t start = 0; start < one.size();) {
                size_t end = one.find(',', start);
                if (end == std::string::npos) end = one.size();
                const std::string part = trimmed(one.substr(start, end - start));
                if (!part.empty()) heroic.push_back(part);
                start = end + 1;
            }
        }
        e.lists["heroic_abilities"] = heroic;
        addField(e.fields, T("Heroic ability"), heroic.empty() ? jsonStr(o, "heroic_ability") : joinList(heroic, orWord.c_str()));
        e.lists["starting_gear"] = jsonStrings(o, "starting_gear");
        e.lists["nicknames"] = jsonStrings(o, "nicknames");
        // a profession written with its own Gear / Nickname tables does not repeat them as lists: each set (a table row) is one entry
        const std::string english = englishName(o, name);
        if (e.list("starting_gear").empty())
            for (const DataTable& t : e.tables)
                if (t.canon == english + ": Gear")
                    for (const TableRow& r : t.rows)
                        if (!r.cells.empty()) e.lists["starting_gear"].push_back(r.cells.front());
        if (e.list("nicknames").empty())
            for (const DataTable& t : e.tables)
                if (t.canon == english + ": Nickname")
                    for (const TableRow& r : t.rows)
                        if (!r.cells.empty()) e.lists["nicknames"].push_back(r.cells.front());
        if (const json* mg = jsonFind(o, "magic"); mg && mg->is_object()) {
            e.props["magic"] = "1";
            e.props["magic.spells"] = std::to_string(jsonInt(*mg, "spells", 3));
            e.props["magic.tricks"] = std::to_string(jsonInt(*mg, "tricks", 3));
            e.props["magic.spell_rank"] = std::to_string(jsonInt(*mg, "spell_rank", 1));
        }
        extraFields(o, e.fields);
        e.body = jsonStr(o, "description");
    }

    // ---- what a piece of equipment says about itself: rules in the data, words made here ------------------------------------

    // A thing the card names by id (a grip, a supply): its name as shown; a plain text (a homebrew's own) is translated as it always was.
    std::string named(const json& o, const char* key) {
        const std::string v = trimmed(jsonStr(o, key));
        return st.idNames_.contains(v) ? shownName(v) : T(v);
    }

    // The price: "cost_gold", "cost_silver" and "cost_copper" (numbers) and a "cost_note" ("× potency", "/ day"); a card that writes a plain
    // "cost" (a homebrew's) says it as it always did. `english`: the coins in English ("10 silver", what a sheet adds up).
    std::string costText(const json& o, bool english = false) {
        if (!jsonFind(o, "cost_gold") && !jsonFind(o, "cost_silver") && !jsonFind(o, "cost_copper") && !jsonFind(o, "cost_note")) return english ? jsonStr(o, "cost") : T(jsonStr(o, "cost"));
        std::string out;
        for (const char* coin : {"gold", "silver", "copper"})
            if (const int n = jsonInt(o, (std::string("cost_") + coin).c_str()); n > 0) out += (out.empty() ? "" : ", ") + std::to_string(n) + " " + (english ? std::string(coin) : T(coin));
        if (const std::string note = trimmed(jsonStr(o, "cost_note")); !note.empty()) out += (out.empty() || note.starts_with('/') ? "" : " ") + note;
        return out.empty() ? "\xE2\x80\x94" : out;
    }

    // The features of a weapon: ids of what it is ("size_tiny", "feature_toppling"), said one after the other, "Subtle, can be thrown".
    std::string featuresText(const json& o) {
        std::string out;
        for (std::string f : jsonStrings(o, "features")) {
            f = shownName(trimmed(f));
            if (!out.empty() && !f.empty() && f[0] >= 'A' && f[0] <= 'Z') f[0] = static_cast<char>(f[0] - 'A' + 'a');
            out += (out.empty() ? "" : ", ") + f;
        }
        return out;
    }

    void weapon(const json& o, const std::string& name) {
        Entry& e = push(Kind::Weapon, o, name);
        const std::string kind = lowerCopy(jsonStr(o, "kind", "melee"));
        e.subtitle = kind == "ranged" ? T("Ranged weapon") : T("Melee weapon");
        addField(e.fields, T("Grip"), named(o, "grip"));
        addField(e.fields, T("STR requirement"), jsonStr(o, "str_req"));
        addField(e.fields, T("Range"), T(jsonStr(o, "range")));
        addField(e.fields, T("Damage"), jsonStr(o, "damage"));
        // "damage_types": the types of damage the weapon can inflict (one or several: a sword slashes and pierces); the names are those
        // of the Damage Types entities, so a pack can add a type and use it here
        e.lists["damage_types"] = references(o, "damage_types", true);
        std::string types;
        for (const std::string& t : e.lists["damage_types"]) types += (types.empty() ? "" : ", ") + T(t);
        addField(e.fields, T("Damage type"), types);
        addField(e.fields, T("Features"), featuresText(o));              // what is left to say: the damage types have their own field
        addField(e.fields, T("Durability"), jsonStr(o, "durability"));
        addField(e.fields, T("Cost"), costText(o));
        addField(e.fields, T("Supply"), named(o, "supply"));
        extraFields(o, e.fields);
        for (const char* p : {"range", "damage", "durability", "str_req"}) e.props[p] = jsonStr(o, p);
        e.props["grip"] = shownName(trimmed(jsonStr(o, "grip")), true);                  // (in English: a sheet keeps it so)
        e.props["cost"] = costText(o, true);
        e.props["kind"] = kind;
    }

    void armor(const json& o, const std::string& name) {
        Entry& e = push(Kind::Armor, o, name);
        const std::string slot = lowerCopy(jsonStr(o, "slot", "armor")) == "helmet" ? "helmet" : "armor";
        e.subtitle = slot == "helmet" ? T("Helmet") : T("Armor");
        addField(e.fields, T("Armor rating"), jsonStr(o, "armor_rating"));
        // "armor_bonuses": a bonus to the armor rating against one type of damage ([{"damage_type": "Slashing", "bonus": 2}]); several
        // are allowed. The type is the name of a Damage Types entity. Kept as JSON in the prop "armor_bonuses" for the rules to use.
        std::string bonuses;
        json list = json::array();
        if (const json* arr = jsonFind(o, "armor_bonuses"); arr && arr->is_array())
            for (const json& b : *arr) {
                const std::string type = b.is_object() ? shownName(trimmed(jsonStr(b, "damage_type")), true) : std::string();
                const int bonus = b.is_object() ? jsonInt(b, "bonus", 0) : 0;
                if (type.empty() || bonus == 0) {
                    warn("armor \"" + name + "\": an armor bonus needs a \"damage_type\" and a \"bonus\" (skipped)");
                    continue;
                }
                list.push_back({{"damage_type", type}, {"bonus", bonus}});
                bonuses += (bonuses.empty() ? "" : ", ") + T(std::string(bonus > 0 ? "+" : "") + std::to_string(bonus) + " against " + type);
            }
        addField(e.fields, T("Bonus against damage"), bonuses);
        // "banes": the skills (or "Ranged attacks") that get a bane while this is worn; a sheet applies them by itself when it is equipped
        e.lists["banes"] = references(o, "banes", true);
        std::string banes;
        for (const std::string& b : references(o, "banes")) banes += (banes.empty() ? "" : ", ") + T(b);      // (as shown: the skills' own names)
        addField(e.fields, T("Bane on"), banes);
        addField(e.fields, T("Cost"), costText(o));
        addField(e.fields, T("Supply"), named(o, "supply"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "effect");
        e.props["slot"] = slot;
        e.props["cost"] = costText(o, true);
        e.props["armor_rating"] = jsonStr(o, "armor_rating");
        if (!list.empty()) e.props["armor_bonuses"] = list.dump();
    }

    void gear(const json& o, const std::string& name) {
        Entry& e = push(Kind::Gear, o, name);
        const std::string category = jsonStr(o, "category");
        e.subtitle = category.empty() ? T("Gear") : T("Gear") + " · " + category;
        addField(e.fields, T("Cost"), costText(o));
        addField(e.fields, T("Supply"), named(o, "supply"));
        addField(e.fields, T("Weight"), jsonStr(o, "weight"));
        extraFields(o, e.fields);
        e.body = jsonStr(o, "effect");
        e.props["category"] = category;
        e.props["cost"] = costText(o, true);
        if (jsonFind(o, "weight")) e.props["weight"] = jsonStr(o, "weight");       // what it weighs, whatever the label says (catalogWeight)
    }

    // ---- creatures ---------------------------------------------------------------------------------

    // What a stat block says. A key of the rules ("core_hp", "core_size", "monster_ferocity": what every creature has, or what the creatures of
    // this game have) is a number or the id of something, and its label is the word of that rule (LOC_LABEL_HP); any other key is the label
    // itself, with its text.
    // The words of a rule of a stat block, said from its data: the skills with their levels, the damage bonus by attribute, the armor it wears, its
    // spells and abilities (ids), its weapons and attacks (a name, a skill level, a damage). Anything else is a number, or the name of what an id names.
    std::string statValue(const std::string& key, const json& v) {
        const std::string dash = "\xE2\x80\x94";
        const auto join = [](const std::vector<std::string>& parts) {
            std::string out;
            for (const std::string& s : parts) out += (out.empty() ? "" : ", ") + s;
            return out;
        };
        std::vector<std::string> parts;
        if (key == "core_skills" && v.is_object()) {
            for (auto it = v.begin(); it != v.end(); ++it) parts.push_back(shownName(it.key()) + " " + jsonText(it.value()));
            return join(parts);
        }
        if (key == "core_damage_bonus" && v.is_object()) {
            for (auto it = v.begin(); it != v.end(); ++it) {
                const AttributeDef* a = st.attributes_.find(it.key());
                parts.push_back(st.text(a ? a->abbrKey : it.key()) + " +" + jsonText(it.value()));
            }
            return parts.empty() ? dash : join(parts);
        }
        if (key == "monster_typical_armor" && v.is_array()) {
            for (const json& id : v) {
                const Entry* card = st.resolveCard(jsonText(id));
                parts.push_back(shownName(jsonText(id)) + (card && !card->prop("armor_rating").empty() ? " (" + card->prop("armor_rating") + ")" : ""));
            }
            return parts.empty() ? dash : join(parts);
        }
        if ((key == "monster_spells" || key == "monster_abilities") && v.is_array()) {
            for (const json& item : v) {
                if (item.is_object() && !item.empty()) parts.push_back(shownName(item.begin().key()) + " \xC3\x97 " + jsonText(item.begin().value()));      // "Robust × 4"
                else parts.push_back(shownName(jsonText(item)));
            }
            return join(parts);
        }
        if ((key == "monster_typical_weapons" || key == "monster_attack") && v.is_array()) {
            for (const json& w : v)
                if (w.is_object())
                    parts.push_back(jsonStr(w, "name") + " (" + T("skill level") + " " + jsonStr(w, "skill_level") + ", " + T("damage") + " " + jsonStr(w, "damage") + ")");
            return join(parts);
        }
        return shownName(trimmed(jsonText(v)));
    }

    void statFields(const json& b, std::vector<Field>& out) {
        const json* f = jsonFind(b, "fields");
        if (!f) return;
        if (f->is_object()) {
            for (auto it = f->begin(); it != f->end(); ++it) {
                const std::string& key = it.key();
                const bool rule = key.starts_with("core_") || key.starts_with("monster_");
                if (!rule) {
                    addField(out, key, jsonText(it.value()));
                    continue;
                }
                const std::string name = key.substr(key.find('_') + 1);
                std::string upper;
                for (char c : name) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                std::string labelKey = "LOC_STAT_" + upper;                  // (the word of a rule of a stat block, else the label of a table column)
                if (st.text(labelKey) == labelKey) labelKey = "LOC_LABEL_" + upper;
                std::string label = st.text(labelKey);
                if (label == labelKey) {                                      // no word written for it: its name, as words
                    label = name;
                    for (char& c : label) c = c == '_' ? ' ' : c;
                    if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                }
                addField(out, label, statValue(key, it.value()));
            }
        } else if (f->is_array()) {
            for (const json& e : *f)
                if (e.is_object()) addField(out, jsonStr(e, "label"), jsonStr(e, "value"));
                else if (e.is_array() && e.size() >= 2) addField(out, jsonText(e[0]), jsonText(e[1]));
        }
    }

    void creature(const json& o, const std::string& name) {
        Monster m;
        m.key = makeKey(Kind::Monster, o, name);
        m.sourceId = sourceFor(o, "creature \"" + name + "\"");
        m.name = name;
        m.canon = englishName(o, name);
        m.kind = lowerCopy(jsonStr(o, "kind", "monster"));
        if (m.kind != "npc" && m.kind != "animal") m.kind = "monster";
        m.category = jsonStr(o, "category");
        m.description = jsonStr(o, "description");
        m.quote = jsonStr(o, "quote");
        m.randomEncounter = jsonStr(o, "random_encounter");
        m.adventureSeed = jsonStr(o, "adventure_seed");
        m.statsRef = jsonStr(o, "stats_ref");
        m.attackDice = normalizeDice(jsonStr(o, "attack_dice"));
        pageInfo(o, m.sourceId, m.ref, m.pageNote);
        const std::string image = jsonStr(o, "image");
        if (!image.empty()) {
            if (!safeRelPath(image)) warn("creature \"" + name + "\": image path \"" + image + "\" must stay inside the pack folder");
            else if (!isFile(pk.dir + "/" + image)) warn("creature \"" + name + "\": image \"" + image + "\" not found");
            else {
                m.image = pk.dir + "/" + image;
                const int ip = jsonInt(o, "image_page");
                const SourceInfo* si = st.source(m.sourceId);
                if (si && si->book && ip > 0) m.imageRef = {m.sourceId, ip, 0};
            }
        }
        if (const json* sb = jsonFind(o, "statblocks"); sb && sb->is_array()) {
            for (const json& b : *sb) {
                if (!b.is_object()) continue;
                StatBlock s;
                s.variant = jsonStr(b, "variant");
                statFields(b, s.fields);
                std::string note;
                pageInfo(b, m.sourceId, s.ref, note);
                m.blocks.push_back(std::move(s));
            }
        } else if (jsonFind(o, "fields")) {          // shorthand: a single stat block written directly
            StatBlock s;
            statFields(o, s.fields);
            m.blocks.push_back(std::move(s));
        }
        if (const json* at = jsonFind(o, "attacks"); at && at->is_array()) {
            int row = 0;
            for (const json& a : *at) {
                if (!a.is_object()) continue;
                ++row;
                Attack k;
                k.name = jsonStr(a, "name");
                k.text = jsonStr(a, "text");
                if (k.text.empty()) k.text = jsonStr(a, "description");
                if (!k.name.empty() && k.text.rfind(k.name, 0) != 0) k.text = k.name + " " + k.text;
                k.rollText = jsonStr(a, "roll");
                if (k.rollText.empty() && jsonFind(a, "roll_min")) {
                    k.rollMin = jsonInt(a, "roll_min");
                    k.rollMax = jsonInt(a, "roll_max", k.rollMin);
                    k.rollText = k.rollMin == k.rollMax ? std::to_string(k.rollMin)
                                                        : std::to_string(k.rollMin) + "-" + std::to_string(k.rollMax);
                } else if (!parseRoll(k.rollText, k.rollMin, k.rollMax)) {
                    k.rollMin = k.rollMax = row;         // no roll given: rows are numbered in order
                    k.rollText = std::to_string(row);
                }
                std::string note;
                pageInfo(a, m.sourceId, k.ref, note);
                m.attacks.push_back(std::move(k));
            }
        }
        if (const json* ab = jsonFind(o, "abilities"); ab && ab->is_array())
            for (const json& a : *ab)
                if (a.is_object() && !jsonStr(a, "name").empty()) {
                    const std::string kind = jsonStr(a, "kind") == "pc_ability" ? "pc_ability" : "ability";
                    m.abilities.push_back({jsonStr(a, "name"), kind, jsonStr(a, "text", jsonStr(a, "description"))});
                }
        for (const std::string& t : jsonStrings(o, "tables")) m.tables.push_back({0, t});
        m.id = static_cast<int>(st.monsters_.size()) + 1;
        st.byKey_[static_cast<int>(Kind::Monster)][m.key] = m.id;
        st.monsters_.push_back(std::move(m));
        ++pk.counts[static_cast<int>(Kind::Monster)];
    }

    // ---- tables ------------------------------------------------------------------------------------

    void table(const json& o, const std::string& name) { addTable(o, name, 0, 0); }

    // `ruleId`: the rule the table is written inside of (its "tables"), 0 for a table of tables.json; `inheritedSource`: that rule's source.
    void addTable(const json& o, const std::string& name, int ruleId, int inheritedSource) {
        DataTable t;
        t.key = makeKey(Kind::Table, o, name);
        t.title = name;
        t.canon = englishName(o, name);
        t.rule = ruleId;
        t.sourceId = jsonStr(o, "source").empty() && inheritedSource ? inheritedSource : sourceFor(o, "table \"" + name + "\"");
        fillTable(o, t);
        t.id = kPackTableBase + static_cast<int>(st.tables_.size()) + 1;
        st.byKey_[static_cast<int>(Kind::Table)][t.key] = t.id;
        st.tables_.push_back(std::move(t));
        ++pk.counts[static_cast<int>(Kind::Table)];
    }

    // What a table says about itself and its rows (its key, title, source and place are set by the caller).
    void fillTable(const json& o, DataTable& t) {
        t.role = lowerCopy(jsonStr(o, "role"));
        t.browse = jsonBool(o, "browse", true);
        t.dice = normalizeDice(jsonStr(o, "dice"));
        t.columns = jsonStrings(o, "columns");
        pageInfo(o, t.sourceId, t.ref, t.pageNote);
        const json* rows = jsonFind(o, "rows");
        int index = 0;
        if (rows && rows->is_array())
            for (const json& r : *rows) {
                TableRow row;
                ++index;
                if (r.is_object()) {
                    row.cells = jsonStrings(r, "cells");
                    row.rollText = jsonStr(r, "roll");
                } else if (r.is_array()) {
                    for (const json& c : r) row.cells.push_back(jsonText(c));
                } else {
                    row.cells.push_back(jsonText(r));
                }
                if (!t.dice.empty()) {
                    if (row.rollText.empty() || !parseRoll(row.rollText, row.rollMin, row.rollMax)) {
                        row.rollMin = row.rollMax = index;
                        row.rollText = std::to_string(index);
                    }
                }
                t.rows.push_back(std::move(row));
            }
        if (t.columns.empty() && !t.rows.empty()) t.columns.assign(t.rows.front().cells.size(), "");
    }

    // ---- rules -------------------------------------------------------------------------------------
    // Rules are written as a tree: a rule lists its "children" inside itself, and a child without its own "source" has its parent's.
    // A rule at the top of the file goes to the top of the tree, or under "parent" (an id of this pack, or the full key
    // "<pack>/rule/<id>" of a rule an earlier pack loaded) so a homerule can hang from a rule of the book. With "replaces" (a full
    // key, or an id of this pack) a rule takes the place of that one instead: same spot in the tree, new title and text (its
    // children, if it has any, are added below), and the tree says which pack changed it.
    static constexpr int kMaxRuleDepth = 24;

    std::string ruleKeyOf(const std::string& ref) const {
        const std::string slug = ref.contains('/') ? ref : keyPartOfId(ref);
        return slug.contains('/') ? slug : pk.id + "/rule/" + slug;
    }

    void rule(const json& o, const std::string& name) { addRule(o, name, 0, 0, 1); }

    // `parentId`: the rule this one is written inside of (0 at the top of the file); `inheritedSource`: that rule's source.
    void addRule(const json& o, const std::string& name, int parentId, int inheritedSource, int depth) {
        if (depth > kMaxRuleDepth) {
            warn("rule \"" + name + "\": written more than " + std::to_string(kMaxRuleDepth) + " levels deep (skipped)");
            return;
        }
        RuleNode n;
        n.title = name;
        n.canon = englishName(o, name);
        if (const json* body = jsonFind(o, "body")) n.body = trimmed(jsonText(*body));
        n.sourceId = jsonStr(o, "source").empty() && inheritedSource ? inheritedSource : sourceFor(o, "rule \"" + name + "\"");
        pageInfo(o, n.sourceId, n.ref, n.pageNote);
        // "see": what this rule points to in other data (a category, an entry key, a rule key); any other key is data for the program
        if (const json* see = jsonFind(o, "see")) {
            if (see->is_string()) n.see.push_back(see->get<std::string>());
            else if (see->is_array())
                for (const json& s : *see)
                    if (s.is_string()) n.see.push_back(s.get<std::string>());
        }
        static const std::set<std::string> known = {"id", "name", "body", "source", "page", "parent", "replaces", "see", "tables", "sections", "children", "_key", "_category_key"};
        // "sections": extra named parts of the same page ("Mages", "Starting Scores"...): {"name", "body"}, shown after the main body.
        if (const json* sections = jsonFind(o, "sections"); sections && sections->is_array())
            for (const json& sec : *sections) {
                const std::string secName = trimmed(jsonStr(sec, "name"));
                if (secName.empty()) {
                    warn("rule \"" + name + "\": a section has no \"name\", skipped");
                    continue;
                }
                RuleNode::Section s;
                s.title = secName;
                s.canon = englishName(sec, secName);
                if (const json* b = jsonFind(sec, "body")) s.body = trimmed(jsonText(*b));
                n.sections.push_back(std::move(s));
            }
        for (auto it = o.begin(); it != o.end(); ++it)
            if (!known.count(it.key())) n.props[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
        int self = 0;
        if (const std::string target = jsonStr(o, "replaces"); !target.empty()) {
            const auto it = st.ruleByKey_.find(ruleKeyOf(target));
            if (it == st.ruleByKey_.end()) {
                warn("rule \"" + name + "\": replaces \"" + target + "\", which is not loaded (skipped)");
                return;
            }
            self = it->second;
            RuleNode& old = st.rules_[static_cast<size_t>(self) - 1];
            old.title = n.title;
            old.canon = n.canon;
            old.body = n.body;
            old.sourceId = n.sourceId;
            old.ref = n.ref;
            old.pageNote = n.pageNote;
            if (!n.see.empty()) old.see = n.see;
            if (!n.sections.empty()) old.sections = n.sections;
            for (const auto& [key, value] : n.props) old.props[key] = value;
            old.editedBy = pk.name;
        } else {
            n.key = keyIn("rule", usedRuleIds, o, name);
            if (parentId) {
                n.parent = parentId;
            } else if (const std::string parent = jsonStr(o, "parent"); !parent.empty()) {
                const auto it = st.ruleByKey_.find(ruleKeyOf(parent));
                if (it != st.ruleByKey_.end()) n.parent = it->second;
                else warn("rule \"" + name + "\": parent \"" + parent + "\" is not loaded before it (put at the top)");
            }
            if (n.parent) n.level = st.rules_[static_cast<size_t>(n.parent) - 1].level + 1;
            n.id = self = static_cast<int>(st.rules_.size()) + 1;
            st.ruleByKey_[n.key] = n.id;
            st.rules_.push_back(std::move(n));
        }
        ++pk.rules;
        if (const json* tables = jsonFind(o, "tables"); tables && tables->is_array()) {
            int index = 0;
            for (const json& t : *tables) {
                ++index;
                const std::string tableName = t.is_object() ? trimmed(jsonStr(t, "name")) : std::string();
                if (tableName.empty()) warn("rule \"" + name + "\": table #" + std::to_string(index) + " has no \"name\", skipped");
                else addTable(t, tableName, self, st.rules_[static_cast<size_t>(self) - 1].sourceId);
            }
        }
        const json* kids = jsonFind(o, "children");
        if (!kids || !kids->is_array()) return;
        int index = 0;
        for (const json& c : *kids) {
            const std::string childName = c.is_object() ? trimmed(jsonStr(c, "name")) : std::string();
            if (childName.empty()) {
                warn("rule \"" + name + "\": child #" + std::to_string(++index) + " has no \"name\", skipped");
                continue;
            }
            ++index;
            addRule(c, childName, self, st.rules_[static_cast<size_t>(self) - 1].sourceId, depth + 1);
        }
    }

    // ------------------------------------------------------------------------------------------------

    // What turns one object of a kind's data file into content. A new kind: its loader here, its row in model.cpp's table.
    using LoadFn = void (Loader::*)(const json&, const std::string&);
    static LoadFn loaderFor(Kind k) {
        switch (k) {
            case Kind::Spell: return &Loader::spell;
            case Kind::Ability: return &Loader::ability;
            case Kind::Skill: return &Loader::skill;
            case Kind::Kin: return &Loader::kin;
            case Kind::Profession: return &Loader::profession;
            case Kind::Weapon: return &Loader::weapon;
            case Kind::Armor: return &Loader::armor;
            case Kind::Gear: return &Loader::gear;
            case Kind::Table: return &Loader::table;
            case Kind::Monster: return &Loader::creature;
        }
        return &Loader::table;
    }

    // Core only: the lists of the random NPC generator, data/system/npcs.yaml ({"npc": {"<list>": {"dice": "D6", "values": [...]}}}).
    void npcs() {
        if (sysDir.empty()) return;
        const std::string path = sysDir + "/npcs.yaml";
        std::string text, err;
        json root;
        if (!isFile(path) || !readTextFile(path, text, &err) || !parseLocalized(sysDir, "npcs.yaml", text, root, err)) return;
        const json* npc = jsonFind(root, "npc");
        if (!npc || !npc->is_object()) return;
        st.npcLists_.clear();
        for (const char* key : {"name", "attitude", "kin", "motivation", "profession", "trait"}) {
            const json* l = jsonFind(*npc, key);
            const json* vals = l ? jsonFind(*l, "values") : nullptr;
            if (!vals || !vals->is_array()) continue;
            NpcList list;
            list.key = key;
            list.dice = jsonStr(*l, "dice");
            for (const json& v : *vals)
                if (v.is_string() && !trimmed(v.get<std::string>()).empty()) list.values.push_back(trimmed(v.get<std::string>()));
            if (!list.values.empty()) st.npcLists_.push_back(std::move(list));
        }
    }

    // Any data file may carry loose tables of its own: a top-level "tables" list shaped like tables.yaml's (id, name, dice, columns, rows,
    // browse: false...). That is how a table lives next to what it is about (movement.yaml, improvised-weapons.yaml...); a rule shows it where its
    // text has a "{{table: Name}}" line. Core's data/system files count too; tables.yaml itself is loaded as the Table kind.
    // The ids of the rows of the files of entities ("damageTypes_slashing") are known before any card loads, so a card can name one
    // (a weapon's damage types). A file that cannot be read is reported later, by looseTables.
    void registerEntityIds() {
        std::vector<std::string> dirs;
        if (!sysDir.empty()) dirs.push_back(sysDir);
        dirs.push_back(pk.dir);
        for (const std::string& dir : dirs)
            for (const std::string& file : fs::listDir(dir)) {
                if (!file.ends_with(".yaml") || file == "manifest.yaml") continue;
                std::string text, err;
                if (!readTextFile(dir + "/" + file, text, &err) || text.find("entities:") == std::string::npos) continue;
                json root;
                if (!parseLocalized(dir, file, text, root, err) || !root.is_object()) continue;
                const json* rows = jsonFind(root, "entities");
                if (!rows || !rows->is_array()) continue;
                for (const json& row : *rows) {
                    const std::string id = trimmed(jsonStr(row, "id")), name = trimmed(jsonText(row.contains("name") ? row["name"] : json("")));
                    if (!id.empty() && !name.empty()) st.idNames_[id] = {name, englishName(row, name)};
                }
            }
    }

    // The ages (age.yaml): rules for the program (trained skills, what it does to the attributes) in each entity's "fields"; the table of ages the
    // reader sees is made from them (ageTable). A later pack may add an age or replace one by its id.
    void loadAges() {
        std::vector<std::string> dirs;
        if (!sysDir.empty()) dirs.push_back(sysDir);
        dirs.push_back(pk.dir);
        for (const std::string& dir : dirs) {
            std::string text, err;
            json root;
            if (!readTextFile(dir + "/age.yaml", text, &err) || !parseLocalized(dir, "age.yaml", text, root, err) || !root.is_object()) continue;
            const json* rows = jsonFind(root, "entities");
            if (!rows || !rows->is_array()) continue;
            for (const json& row : *rows) {
                const std::string id = keyPartOfId(trimmed(jsonStr(row, "id"))), name = trimmed(jsonText(row.contains("name") ? row["name"] : json("")));
                if (id.empty() || name.empty()) continue;
                AgeDef a;
                a.id = id;
                a.name = name;
                a.english = englishName(row, name);
                if (const json* f = jsonFind(row, "fields"); f && f->is_object()) {
                    a.professionSkills = jsonInt(*f, "trained_skills_profession", a.professionSkills);
                    a.freeSkills = jsonInt(*f, "trained_skills_free", a.freeSkills);
                    if (const json* mods = jsonFind(*f, "mods"); mods && mods->is_object())
                        for (auto it = mods->begin(); it != mods->end(); ++it) {
                            const std::string attribute = st.attributes_.idOf(it.key());
                            if (!attribute.empty() && it.value().is_number()) a.mods.emplace_back(attribute, it.value().get<int>());
                        }
                }
                const auto same = std::ranges::find_if(st.ages_, [&](const AgeDef& x) { return x.id == a.id; });
                if (same != st.ages_.end()) *same = a;
                else st.ages_.push_back(a);
            }
        }
    }

    // The entities of age.yaml as the table the reader sees: the words of each age's rules (its trained skills and its effect on the attributes).
    void ageTable(json& root) {
        json* rows = root.contains("entities") && root["entities"].is_array() ? &root["entities"] : nullptr;
        if (!rows) return;
        for (json& row : *rows) {
            const std::string id = keyPartOfId(trimmed(jsonStr(row, "id")));
            const auto it = std::ranges::find_if(st.ages_, [&](const AgeDef& a) { return a.id == id; });
            if (it == st.ages_.end()) continue;
            const std::string effect = st.ageEffect(*it);
            row["fields"] = json::object({{st.text("LOC_LABEL_TRAINED_SKILLS"), std::to_string(it->professionSkills) + "+" + std::to_string(it->freeSkills)},
                                          {st.text("LOC_LABEL_ATTRIBUTES"), effect.empty() ? "\xE2\x80\x94" : effect}});
        }
    }

    bool looseTables() {
        std::vector<std::string> dirs;
        if (!sysDir.empty()) dirs.push_back(sysDir);
        dirs.push_back(pk.dir);
        for (const std::string& dir : dirs)
            for (const std::string& file : fs::listDir(dir)) {
                if (!file.ends_with(".yaml")) continue;
                const std::string stem = file.substr(0, file.size() - 5);
                if (stem == "manifest" || stem == kindFile(Kind::Table)) continue;
                std::string text, err;
                if (!readTextFile(dir + "/" + file, text, &err)) {
                    pk.error = file + ": " + err;
                    return false;
                }
                const auto hasKey = [&](const std::string& key) {                  // written bare (YAML) or quoted (what the GM page writes)
                    const std::string bare = key + ":", quoted = "\"" + key + "\":";
                    return text.starts_with(bare) || text.find("\n" + bare) != std::string::npos || text.find(quoted) != std::string::npos;
                };
                if (!hasKey("tables") && !hasKey("entities")) continue;
                json root;
                if (!parseLocalized(dir, file, text, root, err)) {
                    pk.error = file + ": " + err;
                    return false;
                }
                if (!root.is_object()) continue;
                if (jsonBool(root, "hidden")) continue;                      // (a list of words for the cards to name: grips, supplies...)
                if (jsonStr(root, "kind") == "age") ageTable(root);
                json fileDefaults = json::object();                 // a file of entities may say their "kind", "category" or "source" once, at its top
                for (const char* key : {"kind", "category", "source", "_category_key"})
                    if (const json* v = jsonFind(root, key); v && v->is_string()) fileDefaults[key] = *v;
                const json tables = tablesOf(root, file, categoryFromFile(file), fileDefaults);
                int index = 0;
                for (const json& t : tables) {
                    ++index;
                    const std::string name = t.is_object() ? trimmed(jsonStr(t, "name")) : std::string();
                    if (name.empty()) warn(file + " tables: table #" + std::to_string(index) + " has no \"name\", skipped");
                    else table(t, name);
                }
            }
        return true;
    }

    // actions.yaml (docs/HOMEBREW.md): the actions of play, in groups ("Actions", "Free Actions", "Movement"...). They become the "Actions"
    // chapter of the rules, one page per group with its actions as a table, so they reach every view like any rule. A pack adds actions to
    // a group an earlier pack defined by naming its id, or brings a group of its own.
    //   {"intro": "...", "groups": [{"id", "name", "intro"}], "actions": [{"name", "group", "description"}]}
    bool actions() {
        const std::string path = pk.dir + "/actions.yaml";
        if (!isFile(path)) return true;
        std::string text, err;
        json root;
        if (!readTextFile(path, text, &err) || !parseLocalized(pk.dir, "actions.yaml", text, root, err)) {
            pk.error = "actions.yaml: " + err;
            return false;
        }
        if (jsonInt(root, "format", kFormat) > kFormat) {
            pk.error = "actions.yaml: made for a newer version of the app";
            return false;
        }
        const json* groups = root.is_object() ? jsonFind(root, "groups") : nullptr;
        const json* list = root.is_object() ? jsonFind(root, "actions") : nullptr;
        const bool hasGroups = groups && groups->is_array(), hasList = list && list->is_array();
        if (!hasGroups && !hasList) {
            pk.error = "actions.yaml: expected a \"groups\" list and/or an \"actions\" list";
            return false;
        }
        // The general text of the chapter is its last tab, "General Info": an "intro" at the top of actions.yaml (a body and named
        // sections, like the intro of every other page)
        const json generalInfo = root.contains("intro") ? root["intro"] : json(nullptr);
        const bool newChapter = !st.actionsChapter_;
        if (newChapter) {
            json chapter = {{"id", "actions"}, {"nav", "Reference"}, {"layout", "tabs"}, {"_key", "Actions"}};
            if (const std::string src = jsonStr(root, "source"); !src.empty()) chapter["source"] = src;
            addRule(chapter, T("Actions"), 0, 0, 1);
            st.actionsChapter_ = static_cast<int>(st.rules_.size());
        }
        const int chapterSource = st.rules_[static_cast<size_t>(st.actionsChapter_) - 1].sourceId;
        if (hasGroups) {
            int index = 0;
            for (const json& g : *groups) {
                ++index;
                const std::string id = g.is_object() ? keyPartOfId(jsonStr(g, "id")) : std::string();
                const std::string name = g.is_object() ? trimmed(jsonStr(g, "name")) : std::string();
                if (id.empty() || name.empty()) {
                    warn("actions.yaml: group #" + std::to_string(index) + " needs an \"id\" and a \"name\" (skipped)");
                    continue;
                }
                if (st.actionTables_.count(id)) continue;                  // an earlier pack has it: this pack's actions are added to it
                json page = {{"id", "group-" + id}};
                if (const json* intro = jsonFind(g, "intro")) page["body"] = *intro;
                if (const std::string src = jsonStr(g, "source"); !src.empty()) page["source"] = src;
                // "table": the title of the group's table when it must not be the group's name (the page "Movement" lists the table
                // "Movement Actions", so "{{table: Movement}}" keeps meaning the kin table)
                const std::string tableName = jsonStr(g, "table").empty() ? name : trimmed(jsonStr(g, "table"));
                page["tables"] = json::array({json{{"id", id + "-table"}, {"name", tableName}, {"columns", json::array({T("ACTION"), T("EFFECT")})}, {"rows", json::array()}}});
                if (const std::string en = jsonStr(g, "_key"); !en.empty()) {               // translated: the English titles, as links know them
                    page["_key"] = en;
                    page["tables"][0]["_key"] = jsonStr(g, "table").empty() ? en : trimmed(jsonStr(g, "table"));
                }
                addRule(page, name, st.actionsChapter_, chapterSource, 2);
                st.actionTables_[id] = st.tables_.back().id;
            }
        }
        if (newChapter && !generalInfo.is_null()) {                       // the last tab: a page of text with no table
            json page = {{"id", "general-info"}};
            if (generalInfo.is_string()) {
                page["body"] = generalInfo;
            } else if (generalInfo.is_object()) {
                if (const json* body = jsonFind(generalInfo, "body")) page["body"] = *body;
                if (const json* sections = jsonFind(generalInfo, "sections")) page["sections"] = *sections;
            }
            page["_key"] = "General Info";
            addRule(page, T("General Info"), st.actionsChapter_, chapterSource, 2);
        }
        if (hasList) {
            int index = 0;
            for (const json& a : *list) {
                ++index;
                const std::string name = a.is_object() ? trimmed(jsonStr(a, "name")) : std::string();
                if (name.empty()) {
                    warn("actions.yaml: action #" + std::to_string(index) + " has no \"name\" (skipped)");
                    continue;
                }
                const std::string group = keyPartOfId(jsonStr(a, "group"));
                const auto it = st.actionTables_.find(group);
                if (it == st.actionTables_.end()) {
                    warn("action \"" + name + "\": the group \"" + jsonStr(a, "group") + "\" does not exist (skipped)");
                    continue;
                }
                DataTable& table = st.tables_[static_cast<size_t>(it->second - kPackTableBase) - 1];
                TableRow row;
                row.cells.assign(table.columns.size(), std::string());
                row.cells[0] = name;
                row.cells[1] = a.contains("description") ? trimmed(jsonText(a["description"])) : std::string();
                // "fields": named parts of the action (a parry's Durability, Shield...): each name is one more column of the group's table
                if (const json* fields = jsonFind(a, "fields"); fields && fields->is_object())
                    for (auto f = fields->begin(); f != fields->end(); ++f) {
                        auto col = std::find(table.columns.begin() + 2, table.columns.end(), f.key());
                        if (col == table.columns.end()) {
                            table.columns.push_back(f.key());
                            for (TableRow& other : table.rows) other.cells.push_back(std::string());
                            row.cells.push_back(std::string());
                            col = table.columns.end() - 1;
                        }
                        row.cells[static_cast<size_t>(col - table.columns.begin())] = trimmed(jsonText(f.value()));
                    }
                table.rows.push_back(std::move(row));
            }
        }
        return true;
    }

    void run() {
        setupSources();
        if (!attributeFiles()) return;                    // before the cards: a skill names its attribute by id
        registerEntityIds();                              // and so does a weapon its damage types
        loadAges();                                       // (the reader's table of ages is made from them, in looseTables)
        for (Kind k : kLoadOrder) {
            const auto load = [&](const json& o, const std::string& n) { (this->*loaderFor(k))(o, n); };
            if (!eachObject(kindFile(k), load)) return;
            // the weapons, the armor and the gear may also share one file, equipment.yaml, with a list each (the files of a pack written
            // before it, one per kind, keep working)
            if ((k == Kind::Weapon || k == Kind::Armor || k == Kind::Gear) && !eachObject("equipment", load, kindFile(k))) return;
        }
        if (!looseTables()) return;
        // last: it may replace a rule an earlier pack loaded, and a broken pack must not have touched those
        for (const char* file : {"rules", "world"})       // world.yaml: rules like rules.yaml's, for the world of the game (adventures, NPCs)
            if (!eachObject(file, [&](const json& o, const std::string& n) { rule(o, n); })) return;
        if (!actions()) return;
        npcs();
        pk.loaded = true;
    }
};

void ContentStore::clear() {
    attributes_ = AttributeSet();
    ages_.clear();
    idNames_.clear();
    idKeys_.clear();
    texts_.clear();
    textsEn_.clear();
    packs_.clear();
    sources_.clear();
    for (auto& v : entries_) v.clear();
    for (auto& in : intros_) in = Intro();
    npcLists_.clear();
    actionsChapter_ = 0;
    actionTables_.clear();
    keywords_.clear();
    for (auto& m : byKey_) m.clear();
    monsters_.clear();
    tables_.clear();
    rules_.clear();
    ruleByKey_.clear();
    tablesOfRule_.clear();
    if (index_) sqlite3_close(index_);
    index_ = nullptr;
}

ContentStore::~ContentStore() { clear(); }

// ------------------------------------------------------------------------------------- texts and attributes

std::string ContentStore::englishText(const std::string& key) const {
    const auto it = textsEn_.find(key);
    return it == textsEn_.end() ? key : it->second;
}

const AgeDef& ContentStore::age(const std::string& id) const {
    static const AgeDef none;
    for (const AgeDef& a : ages_)
        if (a.id == id) return a;
    for (const AgeDef& a : ages_)
        if (a.id == "adult") return a;
    return ages_.empty() ? none : ages_.front();
}

std::string ContentStore::ageEffect(const AgeDef& a) const {
    std::vector<int> deltas;                                  // the groups, in the order they are first written
    for (const auto& [id, delta] : a.mods)
        if (!std::ranges::contains(deltas, delta)) deltas.push_back(delta);
    const std::string sentence = " " + i18n::tr(lang_, "and") + " ";
    std::string out;
    for (int delta : deltas) {
        std::vector<std::string> names;
        for (const auto& [id, d] : a.mods)
            if (d == delta) names.push_back(text(attributes_.find(id) ? attributes_.find(id)->abbrKey : id));
        std::string group;
        for (size_t i = 0; i < names.size(); ++i) group += (i == 0 ? "" : i + 1 == names.size() ? sentence : ", ") + names[i];
        out += (out.empty() ? "" : ", ") + group + " " + (delta > 0 ? "+" : "\xE2\x88\x92") + std::to_string(std::abs(delta));
    }
    return out;
}

std::string ContentStore::ageSummary(const AgeDef& a) const {
    const std::string effect = ageEffect(a);
    return (effect.empty() ? i18n::tr(lang_, "no change") : effect) + "; " + std::to_string(a.professionSkills) + "+" + std::to_string(a.freeSkills) + " " + i18n::tr(lang_, "trained skills");
}

std::string ContentStore::text(const std::string& key) const {
    auto it = texts_.find(key);
    std::string out = it != texts_.end() ? it->second : englishText(key);
    // "{agility}" is the short form of that attribute, in this language
    for (size_t open = out.find('{'); open != std::string::npos; open = out.find('{', open + 1)) {
        const size_t close = out.find('}', open);
        if (close == std::string::npos) break;
        const std::string id = out.substr(open + 1, close - open - 1);
        if (const AttributeDef* a = attributes_.find(id); a && a->id == id) {
            const std::string abbr = text(a->abbrKey);
            out.replace(open, close - open + 1, abbr);
            open += abbr.empty() ? 0 : abbr.size() - 1;
        }
    }
    return out;
}

std::string ContentStore::attributeName(const std::string& id) const {
    const AttributeDef* a = attributes_.find(id);
    return a ? text(a->nameKey) : id;
}

std::string ContentStore::attributeAbbr(const std::string& id) const {
    const AttributeDef* a = attributes_.find(id);
    return a ? text(a->abbrKey) : id;
}

std::string ContentStore::attributeDescription(const std::string& id) const {
    const AttributeDef* a = attributes_.find(id);
    return a ? text(a->descriptionKey) : std::string();
}

// How a lookup's value reads in a table: a number, a signed modifier (the book's en dash for a minus), or a bonus ("+D4", "—" for none).
static std::string shownValue(const std::string& format, const std::string& value) {
    const std::string v = trimmed(value);
    if (format == "bonus") return v.empty() ? "\xE2\x80\x94" : "+" + v;
    if (format == "signed") {
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        if (v.empty() || end == v.c_str() || *end != '\0') return v;
        return n > 0 ? "+" + std::to_string(n) : n < 0 ? "\xE2\x80\x93" + std::to_string(-n) : "0";
    }
    return v;
}

std::string ContentStore::lookupShown(const std::string& lookupId, int score) const {
    const Lookup* l = attributes_.lookup(lookupId);
    return l ? shownValue(l->format, attributes_.value(lookupId, score)) : std::string();
}

void ContentStore::load(const std::vector<PackSpec>& specs, const std::string& lang, const ContentStore* english) {
    clear();
    lang_ = i18n::normalize(lang);
    std::set<std::string> seen;
    for (const PackSpec& spec : specs) {
        PackInfo pk;
        pk.core = spec.core;
        pk.enabled = spec.enabled;
        json meta;
        if (!readMeta(spec.dir, pk, meta, lang_)) {
            if (pk.id.empty()) pk.id = spec.dir;
            if (pk.name.empty()) pk.name = pk.id;
            packs_.push_back(std::move(pk));
            continue;
        }
        if (!seen.insert(pk.id).second) {
            pk.error = "another pack already uses the id \"" + pk.id + "\"";
            packs_.push_back(std::move(pk));
            continue;
        }
        if (spec.enabled) {
            // the localization files (i18n/en.csv, i18n/<lang>.csv): Core's system ones first, then the pack's, each over the last
            std::vector<std::string> textDirs;
            if (spec.core) textDirs.push_back(systemDirOf(spec.dir));
            textDirs.push_back(spec.dir);
            for (const std::string& dir : textDirs)
                for (const std::string& l : {std::string(i18n::kDefault), lang_}) {
                    std::string textError;
                    if (!i18n::loadTexts(dir, l, l == i18n::kDefault ? textsEn_ : texts_, &textError)) pk.warnings.push_back(textError);
                }
            const AttributeSet attributesBefore = attributes_;
            // load into the shared vectors; if the pack turns out to be broken, roll everything it added back
            size_t before[kKindCount];
            for (int k = 0; k < kKindCount; ++k) before[k] = entries_[k].size();
            const size_t monBefore = monsters_.size(), tabBefore = tables_.size(), srcBefore = sources_.size(), rulesBefore = rules_.size();
            Loader loader(*this, pk, meta, spec.core ? systemDirOf(spec.dir) : std::string(), lang_);
            loader.run();
            if (!pk.loaded) {
                attributes_ = attributesBefore;
                for (int k = 0; k < kKindCount; ++k) {
                    entries_[k].resize(before[k]);
                    pk.counts[k] = 0;
                }
                pk.rules = 0;
                monsters_.resize(monBefore);
                tables_.resize(tabBefore);
                sources_.resize(srcBefore);
                rules_.resize(rulesBefore);
                pk.sourceIds.clear();
                const std::string prefix = pk.id + "/";
                for (auto& m : byKey_)
                    for (auto it = m.begin(); it != m.end();) it = it->first.starts_with(prefix) ? m.erase(it) : std::next(it);
                for (auto it = ruleByKey_.begin(); it != ruleByKey_.end();) it = it->first.starts_with(prefix) ? ruleByKey_.erase(it) : std::next(it);
            }
        }
        packs_.push_back(std::move(pk));
    }
    extractKeywords();
    // A translated text may still link to a keyword by its English word ("[[boon]]"): the English content's keywords point at the same
    // places here (rules, entries and sections are read in the same order, so their handles are the same).
    if (english && english != this)
        for (const auto& [word, target] : english->keywords()) {
            SeeTarget t = target;
            if (t.type == SeeTarget::Type::Rule) {
                if (const RuleNode* r = rule(t.id)) t.label = r->title;
            } else if (t.type == SeeTarget::Type::Entry) {
                t.label = titleOf(t.kind, t.id);
            }
            keywords_.emplace(word, t);
        }
    placeTables();
    resolveLinks();
    checkRuleLinks();
    buildIndex();
}

SeeTarget ContentStore::seeTarget(const std::string& ref) const {
    SeeTarget t;
    if (Kind k; kindFromFile(ref, k) && k != Kind::Table) {       // a whole category: "spells", "kin"...
        t.type = SeeTarget::Type::Category;
        t.kind = k;
        t.label = i18n::tr(lang_, kindTitle(k)) + " (" + std::to_string(count(k)) + ")";
        return t;
    }
    const size_t a = ref.find('/'), b = a == std::string::npos ? a : ref.find('/', a + 1);      // "<pack>/<kind>/<id>"
    if (b == std::string::npos) return t;
    const std::string what = ref.substr(a + 1, b - a - 1);
    if (what == "rule") {
        if (const int id = ruleByKey(ref)) {
            t.type = SeeTarget::Type::Rule;
            t.id = id;
            t.label = rule(id)->title;
        }
        return t;
    }
    Kind k;
    if (!kindFromKey(what, k)) return t;
    if (const int id = idByKey(k, ref)) {
        t.type = SeeTarget::Type::Entry;
        t.kind = k;
        t.id = id;
        t.label = titleOf(k, id);
    }
    return t;
}

// A keyword is a word marked where it is explained: "{{key: boon}}" (shown as "boon") or "{{key: boons | boon}}" (shown as "boons", the
// keyword is "boon"). The markers are taken out of the texts here, so everything that shows or searches them sees plain words; the
// keyword remembers the text it was in. The first text to mark a word keeps it.
void ContentStore::extractKeywords() {
    keywords_.clear();
    auto strip = [&](std::string& text, const SeeTarget& owner) {
        for (size_t at = text.find("{{key:"); at != std::string::npos; at = text.find("{{key:", at)) {
            const size_t end = text.find("}}", at);
            if (end == std::string::npos) break;
            std::string inner = text.substr(at + 6, end - at - 6);
            std::string shown = inner, word = inner;
            if (const size_t bar = inner.find('|'); bar != std::string::npos) {
                shown = inner.substr(0, bar);
                word = inner.substr(bar + 1);
            }
            shown = trimmed(shown);
            word = lowerCopy(trimmed(word));
            if (!word.empty()) {
                SeeTarget at_ = owner;
                at_.line = static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
                keywords_.emplace(word, at_);
            }
            text.replace(at, end + 2 - at, shown);
            at += shown.size();
        }
    };
    for (int k = 0; k < kKindCount; ++k) {
        Intro& in = intros_[k];
        SeeTarget owner;
        owner.kind = static_cast<Kind>(k);
        owner.type = SeeTarget::Type::Category;
        owner.label = i18n::tr(lang_, kindTitle(owner.kind));
        strip(in.body, owner);
        for (size_t s = 0; s < in.sections.size(); ++s) {
            owner.type = SeeTarget::Type::Section;
            owner.id = static_cast<int>(s);
            owner.label = in.sections[s].title;
            strip(in.sections[s].body, owner);
        }
    }
    for (RuleNode& r : rules_) {
        SeeTarget owner;
        owner.type = SeeTarget::Type::Rule;
        owner.id = r.id;
        owner.label = r.title;
        strip(r.body, owner);
        for (size_t s = 0; s < r.sections.size(); ++s) {                    // a keyword in a section points at that section
            owner.section = static_cast<int>(s);
            strip(r.sections[s].body, owner);
        }
    }
    for (DataTable& t : tables_) {                                             // a keyword in a table cell points at the rule that shows the table
        if (!t.rule) continue;
        SeeTarget owner;
        owner.type = SeeTarget::Type::Rule;
        owner.id = t.rule;
        owner.label = rules_[static_cast<size_t>(t.rule) - 1].title;
        for (TableRow& r : t.rows)
            for (std::string& cell : r.cells) strip(cell, owner);
    }
    for (int k = 0; k < kKindCount; ++k)
        for (Entry& e : entries_[k]) {
            SeeTarget owner;
            owner.type = SeeTarget::Type::Entry;
            owner.kind = e.kind;
            owner.id = e.id;
            owner.label = e.title;
            strip(e.body, owner);
        }
}

SeeTarget ContentStore::resolveLink(const std::string& ref) const {
    if (const auto kw = keywords_.find(lowerCopy(trimmed(ref))); kw != keywords_.end()) return kw->second;
    if (SeeTarget t = seeTarget(ref); t.type != SeeTarget::Type::None) return t;
    const std::string want = lowerCopy(trimmed(ref));
    SeeTarget t;
    if (want.empty()) return t;
    auto sectionOf = [&](Kind k, const std::string& title) {
        const Intro& in = intros_[static_cast<int>(k)];
        for (size_t s = 0; s < in.sections.size(); ++s)
            if (lowerCopy(in.sections[s].title) == title || lowerCopy(in.sections[s].canon) == title) {
                SeeTarget out;
                out.type = SeeTarget::Type::Section;
                out.kind = k;
                out.id = static_cast<int>(s);
                out.label = in.sections[s].title;
                return out;
            }
        return SeeTarget();
    };
    if (const size_t slash = want.find('/'); slash != std::string::npos)     // "skills/Pushing Your Roll"
        if (Kind k; kindFromFile(want.substr(0, slash), k) && k != Kind::Table) return sectionOf(k, trimmed(want.substr(slash + 1)));
    for (int i = 0; i < kKindCount; ++i) {                                  // "Spells", "creatures"
        const Kind k = static_cast<Kind>(i);
        if (k == Kind::Table || (lowerCopy(kindTitle(k)) != want && lowerCopy(i18n::tr(lang_, kindTitle(k))) != want)) continue;
        return seeTarget(kindFile(k));
    }
    for (int i = 0; i < kKindCount; ++i) {                                  // "Fighter", "Birdsong"
        const Kind k = static_cast<Kind>(i);
        if (const Entry* e = findByName(k, want)) return seeTarget(e->key);
    }
    for (const RuleNode& r : rules_)                                        // "Melee Combat"
        if (lowerCopy(r.title) == want) return seeTarget(r.key);
    for (const RuleNode& r : rules_)                                        // its English title, in a translated text
        if (lowerCopy(r.canon) == want) return seeTarget(r.key);
    for (int i = 0; i < kKindCount; ++i)                                    // "Pushing Your Roll"
        if (SeeTarget s = sectionOf(static_cast<Kind>(i), want); s.type != SeeTarget::Type::None) return s;
    return t;
}

// A rule that points to something that is not there is a mistake worth a warning (the pack still loads).
void ContentStore::checkRuleLinks() {
    for (const RuleNode& r : rules_)
        for (const std::string& ref : r.see) {
            if (seeTarget(ref).type != SeeTarget::Type::None) continue;
            const std::string packId = r.key.substr(0, r.key.find('/'));
            for (PackInfo& p : packs_)
                if (p.id == packId && p.warnings.size() < kMaxWarnings) p.warnings.push_back("rule \"" + r.title + "\": it says to see \"" + ref + "\", which is not there");
        }
}

// Every browsable table is shown inside a rule. Those written in a rule's "tables" already are; those of a pack's tables.json get a rule
// of their own per pack, "Tables · <pack>", at the end of the tree.
void ContentStore::placeTables() {
    std::map<std::string, int> homeOf;                       // pack id -> the rule that holds its loose tables
    // a loose table that some text already shows with a "{{table: Name}}" line has its place: it needs no listing of its own
    std::set<std::string> placed;
    auto scan = [&](const std::string& text) {
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = trimmed(text.substr(pos, end - pos));
            if (line.starts_with("{{table:") && line.ends_with("}}")) placed.insert(lowerCopy(trimmed(line.substr(8, line.size() - 10))));
            pos = end + 1;
        }
    };
    for (const RuleNode& r : rules_) {
        scan(r.body);
        for (const auto& s : r.sections) scan(s.body);
    }
    for (const Intro& in : intros_) {
        scan(in.body);
        for (const auto& s : in.sections) scan(s.body);
    }
    for (const auto& list : entries_)
        for (const Entry& e : list) scan(e.body);
    for (DataTable& t : tables_) {
        if (!t.rule && t.browse && !placed.count(lowerCopy(t.title)) && !placed.count(lowerCopy(t.canon))) {
            const std::string packId = t.key.substr(0, t.key.find('/'));
            auto home = homeOf.find(packId);
            if (home == homeOf.end()) {
                const PackInfo* pk = pack(packId);
                RuleNode n;
                n.key = packId + "/rule/other-tables";
                for (int i = 2; ruleByKey_.count(n.key); ++i) n.key = packId + "/rule/other-tables-" + std::to_string(i);
                n.title = i18n::tr(lang_, "Tables") + " · " + (pk ? pk->name : packId);
                n.canon = "Tables · " + (pk ? pk->name : packId);
                n.sourceId = t.sourceId;
                n.id = static_cast<int>(rules_.size()) + 1;
                ruleByKey_[n.key] = n.id;
                home = homeOf.emplace(packId, n.id).first;
                rules_.push_back(std::move(n));
            }
            t.rule = home->second;
        }
        if (t.rule) tablesOfRule_[t.rule].push_back(t.id);
    }
}

const std::vector<int>& ContentStore::tablesOfRule(int ruleId) const {
    static const std::vector<int> none;
    const auto it = tablesOfRule_.find(ruleId);
    return it == tablesOfRule_.end() ? none : it->second;
}

// Things that need every pack loaded: creature -> related tables, kin -> their innate abilities' text.
void ContentStore::resolveLinks() {
    std::map<std::string, int> byTitle;                      // "<source>|<title>" and "<title>": the first table with that title
    for (const DataTable& t : tables_)
        for (const std::string* title : {&t.title, &t.canon}) {                 // (a creature of a translation may name it in English)
            if (title->empty()) continue;
            byTitle.emplace(std::to_string(t.sourceId) + "|" + lowerCopy(*title), t.id);
            byTitle.emplace(lowerCopy(*title), t.id);
        }
    for (Monster& m : monsters_) {
        std::vector<TableRef> resolved;
        for (const TableRef& t : m.tables) {
            // the creature's own table first ("Goblin: First Name" for "First Name"), then any table with that title
            auto it = byTitle.find(std::to_string(m.sourceId) + "|" + lowerCopy(m.name + ": " + t.title));
            if (it == byTitle.end()) it = byTitle.find(std::to_string(m.sourceId) + "|" + lowerCopy(m.canon + ": " + t.title));
            if (it == byTitle.end()) it = byTitle.find(std::to_string(m.sourceId) + "|" + lowerCopy(t.title));
            if (it == byTitle.end()) it = byTitle.find(lowerCopy(t.title));
            if (it != byTitle.end()) resolved.push_back({it->second, packTable(it->second) ? packTable(it->second)->title : t.title});
        }
        m.tables = std::move(resolved);
    }
    // What one card names of another is a link ("[[Name|key]]", as the texts write them), so a field or a text can lead to it.
    const auto link = [](const Entry& a) { return "[[" + a.title + "|" + a.key + "]]"; };
    const auto linked = [&](Kind k, const std::string& name) {              // a name that is no card of that kind stays as it is
        const Entry* a = findByName(k, name);
        return a ? link(*a) : name;
    };
    const auto joined = [&](Kind k, const std::vector<std::string>& names, const std::string& sep) {
        std::string out;
        for (const std::string& n : names) out += (out.empty() ? "" : sep) + linked(k, n);
        return out;
    };
    const auto setField = [&](Entry& e, const char* english, const std::string& value) {
        const std::string label = i18n::tr(lang_, english);
        for (Field& f : e.fields)
            if (f.label == label) {
                f.value = value;
                return;
            }
    };
    // A kin's innate abilities are cards of their own: the kin's text includes each one ("{{include: key}}"), unless the text already
    // says where with its own include line.
    for (Entry& k : entries_[static_cast<int>(Kind::Kin)]) {
        std::set<const Entry*> placed;
        size_t pos = 0;
        while (pos <= k.body.size()) {
            size_t end = k.body.find('\n', pos);
            if (end == std::string::npos) end = k.body.size();
            if (const Entry* c = resolveCard(includeMarkerRef(k.body.substr(pos, end - pos)))) placed.insert(c);
            pos = end + 1;
        }
        for (const std::string& name : k.list("innate_abilities")) {
            const Entry* a = findByName(Kind::Ability, name);
            if (!a || placed.count(a)) continue;
            k.body += (k.body.empty() ? "" : "\n\n") + std::string("{{include: ") + a->key + "}}";
        }
    }
    for (Entry& p : entries_[static_cast<int>(Kind::Profession)]) {
        if (!p.list("skills").empty()) setField(p, "Skills", joined(Kind::Skill, p.list("skills"), ", "));
        for (const std::string& school : p.list("schools"))
            setField(p, (school + " skills").c_str(), joined(Kind::Skill, p.list("skills." + school), ", "));
        if (!p.list("heroic_abilities").empty()) setField(p, "Heroic ability", joined(Kind::Ability, p.list("heroic_abilities"), " " + i18n::tr(lang_, "or") + " "));
    }
    // What a card requires names other cards: "skills_knives" + rank 12 reads "Knives 12", a link to the skill. Words that name no card
    // ("Any melee weapon skill", a homebrew's own text) stay as they are.
    const auto refText = [&](const std::string& s) {
        const Entry* c = resolveCard(s);
        return c ? link(*c) : s;
    };
    const auto listText = [&](const std::vector<std::string>& parts, const std::string& word) {
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : i + 1 == parts.size() ? " " + i18n::tr(lang_, word) + " " : ", ") + parts[i];
        return out;
    };
    const auto expression = [&](const json& v, const std::string& listWord) -> std::string {
        const auto each = [&](const json& list) {
            std::vector<std::string> parts;
            for (const json& x : list) parts.push_back(refText(jsonText(x)));
            return parts;
        };
        if (v.is_array()) return listText(each(v), listWord);
        if (const json* any = jsonFind(v, "any"); any && any->is_array()) return listText(each(*any), "or");
        return refText(jsonText(v));
    };
    const auto parsed = [](const std::string& text) { return json::parse(text, nullptr, false); };
    for (Entry& a : entries_[static_cast<int>(Kind::Ability)]) {
        if (const std::string req = a.prop("requirement.ref"); !req.empty()) {
            const std::string rank = a.prop("requirement.rank");
            setField(a, "Requirement", expression(parsed(req), "or") + (rank.empty() ? "" : " " + rank));
        }
        if (const std::string kin = a.prop("kin"); !kin.empty()) {
            const auto it = idNames_.find(kin);
            if (it != idNames_.end()) a.props["kin"] = it->second.first;
            a.subtitle += " · " + a.prop("kin");
        }
    }
    for (Entry& s : entries_[static_cast<int>(Kind::Spell)])
        if (const std::string pre = s.prop("prerequisite.ref"); !pre.empty()) setField(s, "Prerequisite", expression(parsed(pre), "and"));
    for (Entry& a : entries_[static_cast<int>(Kind::Armor)]) {
        std::string banes;
        for (const std::string& b : a.list("banes")) {
            const Entry* s = findByName(Kind::Skill, b);
            banes += (banes.empty() ? "" : ", ") + (s ? link(*s) : i18n::tr(lang_, b));
        }
        if (!banes.empty()) setField(a, "Bane on", banes);
    }
}

// ------------------------------------------------------------------------------------- lookups

const PackInfo* ContentStore::pack(const std::string& id) const {
    for (const PackInfo& p : packs_)
        if (p.id == id) return &p;
    return nullptr;
}

const SourceInfo* ContentStore::source(int id) const {
    for (const SourceInfo& s : sources_)
        if (s.id == id) return &s;
    return nullptr;
}

int ContentStore::sourceId(const std::string& packId, const std::string& key) const {
    for (const SourceInfo& s : sources_)
        if (s.packId == packId && s.key == key) return s.id;
    return 0;
}

int ContentStore::bookId(const std::string& key) const {
    for (const SourceInfo& s : sources_)
        if (s.book && s.key == key) return s.id;
    return 0;
}

int ContentStore::pageCount(int sourceId) const {
    const SourceInfo* s = source(sourceId);
    return s && s->book ? s->pages : 0;
}

int ContentStore::printedPage(int sourceId, int page) const {
    const SourceInfo* s = source(sourceId);
    return s && page >= 1 && page <= static_cast<int>(s->printedPages.size()) ? s->printedPages[static_cast<size_t>(page) - 1] : 0;
}

const RuleNode* ContentStore::rule(int id) const {
    return id >= 1 && id <= static_cast<int>(rules_.size()) ? &rules_[static_cast<size_t>(id) - 1] : nullptr;
}

int ContentStore::ruleByKey(const std::string& key) const {
    const auto it = ruleByKey_.find(key);
    return it == ruleByKey_.end() ? 0 : it->second;
}

const std::vector<Entry>& ContentStore::entries(Kind k) const { return entries_[static_cast<int>(k)]; }

const Intro& ContentStore::introOf(Kind k) const { return intros_[static_cast<int>(k)]; }

const Entry* ContentStore::entry(Kind k, int id) const {
    const auto& v = entries_[static_cast<int>(k)];
    return id >= 1 && id <= static_cast<int>(v.size()) ? &v[static_cast<size_t>(id) - 1] : nullptr;
}

const Entry* ContentStore::findByName(Kind k, const std::string& name) const {
    const std::string want = lowerCopy(trimmed(name));
    for (const Entry& e : entries_[static_cast<int>(k)])
        if (lowerCopy(e.title) == want) return &e;
    for (const Entry& e : entries_[static_cast<int>(k)])                   // its English name (a sheet keeps the names in English)
        if (lowerCopy(e.canon) == want) return &e;
    return nullptr;
}

std::string includeMarkerRef(const std::string& line) {
    const std::string t = trimmed(line);
    if (!t.starts_with("{{include:") || !t.ends_with("}}")) return {};
    return trimmed(t.substr(10, t.size() - 12));
}

const Entry* ContentStore::resolveCard(const std::string& ref) const {
    const std::string r = trimmed(ref);
    if (r.empty()) return nullptr;
    const std::string wanted = [&] {                         // an id is the key of its card
        const auto it = idKeys_.find(r);
        return it == idKeys_.end() ? r : it->second;
    }();
    for (int k = 0; k < kKindCount; ++k) {
        if (static_cast<Kind>(k) == Kind::Monster || static_cast<Kind>(k) == Kind::Table) continue;
        const auto it = byKey_[k].find(wanted);
        if (it != byKey_[k].end()) return entry(static_cast<Kind>(k), it->second);
    }
    for (int k = 0; k < kKindCount; ++k) {
        if (static_cast<Kind>(k) == Kind::Monster || static_cast<Kind>(k) == Kind::Table) continue;
        if (const Entry* e = findByName(static_cast<Kind>(k), r)) return e;
    }
    return nullptr;
}

const Monster* ContentStore::monster(int id) const {
    return id >= 1 && id <= static_cast<int>(monsters_.size()) ? &monsters_[static_cast<size_t>(id) - 1] : nullptr;
}

const DataTable* ContentStore::packTable(int id) const {
    const int i = id - kPackTableBase;
    return i >= 1 && i <= static_cast<int>(tables_.size()) ? &tables_[static_cast<size_t>(i) - 1] : nullptr;
}

std::vector<const DataTable*> ContentStore::tablesByRole(const std::string& role) const {
    std::vector<const DataTable*> out;
    for (const DataTable& t : tables_)
        if (t.role == role) out.push_back(&t);
    return out;
}

const DataTable* ContentStore::tableByName(const std::string& name) const {
    const std::string want = lowerCopy(trimmed(name));
    if (want.empty()) return nullptr;
    for (const bool english : {false, true}) {                              // its title, else its English title (in a translated text)
        const auto is = [&](const DataTable& t) { return lowerCopy(english ? t.canon : t.title) == want; };
        for (const DataTable& t : tables_)
            if (is(t)) return &t;
        for (const Intro& in : intros_)
            for (const DataTable& t : in.tables)
                if (is(t)) return &t;
        for (const auto& list : entries_)
            for (const Entry& e : list)
                for (const DataTable& t : e.tables)
                    if (is(t)) return &t;
    }
    return nullptr;
}

const DataTable* ContentStore::tableByRole(const std::string& role) const {
    auto all = tablesByRole(role);
    return all.empty() ? nullptr : all.front();
}

std::string ContentStore::keyOf(Kind k, int id) const {
    if (k == Kind::Monster) {
        const Monster* m = monster(id);
        return m ? m->key : std::string();
    }
    if (k == Kind::Table) {
        const DataTable* t = packTable(id);
        return t ? t->key : std::string();
    }
    const Entry* e = entry(k, id);
    return e ? e->key : std::string();
}

int ContentStore::idByKey(Kind k, const std::string& key) const {
    const auto& m = byKey_[static_cast<int>(k)];                             // (a table's old "#12" key is an alias in here)
    auto it = m.find(key);
    return it == m.end() ? 0 : it->second;
}

std::string ContentStore::titleOf(Kind k, int id) const {
    if (k == Kind::Monster) {
        const Monster* m = monster(id);
        return m ? m->name : std::string();
    }
    if (k == Kind::Table) {
        const DataTable* t = packTable(id);
        return t ? t->title : std::string();
    }
    const Entry* e = entry(k, id);
    return e ? e->title : std::string();
}

int ContentStore::count(Kind k) const {
    if (k == Kind::Monster) return static_cast<int>(monsters_.size());
    if (k == Kind::Table) return static_cast<int>(tables_.size());
    return static_cast<int>(entries_[static_cast<int>(k)].size());
}

const Entry* sameEntry(const ContentStore& in, const Entry& e) { return in.entry(e.kind, in.idByKey(e.kind, e.key)); }

const Monster* sameMonster(const ContentStore& in, const Monster& m) { return in.monster(in.idByKey(Kind::Monster, m.key)); }

// ------------------------------------------------------------------------------------- search

void ContentStore::buildIndex() {
    if (sqlite3_open(":memory:", &index_) != SQLITE_OK) {
        index_ = nullptr;
        return;
    }
    sqlite3_exec(index_,
                 "CREATE VIRTUAL TABLE idx USING fts5(kind, ref_id UNINDEXED, title, body, source UNINDEXED, page UNINDEXED,"
                 " tokenize = 'porter unicode61 remove_diacritics 2');",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(index_, "BEGIN", nullptr, nullptr, nullptr);
    Stmt ins(index_, "INSERT INTO idx(kind, ref_id, title, body, source, page) VALUES (?,?,?,?,?,?)");
    auto add = [&](Kind k, int id, const std::string& title, const std::string& body, int source, int page) {
        ins.bind(1, std::string(kindKey(k))).bind(2, id).bind(3, title).bind(4, body).bind(5, source).bind(6, page);
        ins.run();
        ins.reset();
    };
    for (int k = 0; k < kKindCount; ++k)
        for (const Entry& e : entries_[k]) {
            std::string body = e.subtitle + "\n";
            for (const Field& f : e.fields) body += f.label + ": " + f.value + "\n";
            std::string tables;                                          // the card's own tables are searched with it
            for (const DataTable& t : e.tables) {
                tables += "\n" + t.title + "\n";
                for (const TableRow& r : t.rows)
                    for (const std::string& c : r.cells) tables += c + " ";
            }
            if (e.canon != e.title) body += e.canon + "\n";              // translated: its English name finds it too
            add(e.kind, e.id, e.title, body + e.body + tables, e.sourceId, e.ref.page);
        }
    for (const Monster& m : monsters_) {
        std::string body = m.category + "\n" + m.quote + "\n" + m.description + "\n";
        if (m.canon != m.name) body += m.canon + "\n";
        for (const StatBlock& b : m.blocks)
            for (const Field& f : b.fields) body += f.label + ": " + f.value + "\n";
        for (const Attack& a : m.attacks) body += a.text + "\n";
        for (const NamedText& a : m.abilities) body += a.name + " " + a.text + "\n";
        add(Kind::Monster, m.id, m.name, body + m.randomEncounter + "\n" + m.adventureSeed, m.sourceId, m.ref.page);
    }
    for (const DataTable& t : tables_) {
        if (!t.browse) continue;
        std::string body;
        for (const TableRow& r : t.rows) {
            body += r.rollText + " ";
            for (const std::string& c : r.cells) body += c + " | ";
            body += "\n";
        }
        add(Kind::Table, t.id, t.title, body, t.sourceId, t.ref.page);
    }
    sqlite3_exec(index_, "COMMIT", nullptr, nullptr, nullptr);
}

std::vector<Hit> ContentStore::search(const std::string& userText, int limit, const std::vector<Kind>& only) const {
    std::vector<Hit> out;
    const std::string match = ftsMatch(userText);
    if (match.empty() || !index_) return out;
    std::string kinds;                                   // kind names are ours, never user input
    for (Kind k : only) kinds += std::string(kinds.empty() ? "" : ",") + "'" + kindKey(k) + "'";
    const std::string sql = std::string("SELECT kind, ref_id, title, snippet(idx, 3, '\x01', '\x02', ' … ', 14), source, page, "
                                        "lower(title) = lower(?3) FROM idx WHERE idx MATCH ?1 ") +
                            (kinds.empty() ? "" : "AND kind IN (" + kinds + ") ") +
                            "ORDER BY 7 DESC, bm25(idx, 0.0, 0.0, 12.0, 1.0, 0.0, 0.0) LIMIT ?2";
    Stmt q(index_, sql.c_str());
    if (!q.ok()) return out;
    q.bind(1, match).bind(2, limit).bind(3, trimmed(userText));
    while (q.step()) {
        Hit h;
        if (!kindFromKey(q.s(0), h.kind)) continue;
        h.id = q.i(1);
        h.title = q.s(2);
        h.snippet = q.s(3);
        h.sourceId = q.i(4);
        h.page = q.i(5);
        h.exact = q.i(6) != 0;
        out.push_back(std::move(h));
    }
    return out;
}

}  // namespace gm
