#include "parsing/i18n.h"

#include <algorithm>
#include <map>
#include <utility>

#include "parsing/fts.h"

namespace gm::i18n {
namespace {

// A catalog key with placeholders: the literal pieces between them, and which placeholder comes after each piece.
struct Template {
    std::vector<std::string> pieces;     // pieces.size() == slots.size() + 1
    std::vector<int> slots;              // "{0}" -> 0
    std::string value;
    size_t literal = 0;                  // letters that are not placeholders: the longer, the more specific
};

struct Catalog {
    std::map<std::string, std::string> exact;
    std::vector<Template> templates;
};

std::map<std::string, Catalog>& catalogs() {
    static std::map<std::string, Catalog> all;
    return all;
}

thread_local std::string tlCurrent = kDefault;

bool parseTemplate(const std::string& key, Template& out) {
    std::string piece;
    for (size_t i = 0; i < key.size();) {
        if (key[i] == '{') {
            const size_t close = key.find('}', i);
            if (close != std::string::npos && close > i + 1 && close - i <= 3 &&
                std::all_of(key.begin() + static_cast<std::ptrdiff_t>(i) + 1, key.begin() + static_cast<std::ptrdiff_t>(close), [](char c) { return c >= '0' && c <= '9'; })) {
                out.pieces.push_back(piece);
                out.literal += piece.size();
                piece.clear();
                out.slots.push_back(std::atoi(key.substr(i + 1, close - i - 1).c_str()));
                i = close + 1;
                continue;
            }
        }
        piece += key[i++];
    }
    out.pieces.push_back(piece);
    out.literal += piece.size();
    return !out.slots.empty();
}

// What fills each placeholder of `t` in `text`, or nothing when the text does not have that shape.
std::optional<std::vector<std::string>> matchTemplate(const Template& t, const std::string& text) {
    if (!text.starts_with(t.pieces.front()) || text.size() < t.literal) return std::nullopt;
    if (t.pieces.size() > 1 && !text.ends_with(t.pieces.back())) return std::nullopt;
    std::vector<std::string> filled(t.slots.size());
    size_t pos = t.pieces.front().size();
    const size_t end = text.size() - t.pieces.back().size();
    if (end < pos) return std::nullopt;
    for (size_t i = 0; i < t.slots.size(); ++i) {
        const bool last = i + 1 == t.slots.size();
        size_t next = end;
        if (!last) {
            next = text.find(t.pieces[i + 1], pos);
            if (next == std::string::npos || next > end) return std::nullopt;
        }
        filled[i] = text.substr(pos, next - pos);
        pos = next + (last ? 0 : t.pieces[i + 1].size());
    }
    return filled;
}

using Names = std::function<std::string(const std::string&)>;

std::string translate(const std::string& lang, const std::string& english, int depth, const Names* names) {
    if (english.empty() || lang == kDefault) return english;
    const auto cat = catalogs().find(lang);
    if (cat == catalogs().end()) return names && depth > 0 ? (*names)(english) : english;
    if (const auto it = cat->second.exact.find(english); it != cat->second.exact.end()) return it->second;
    if (depth > 3) return english;
    for (const Template& t : cat->second.templates) {
        const auto filled = matchTemplate(t, english);
        if (!filled) continue;
        std::string out = t.value;
        for (size_t i = 0; i < t.slots.size(); ++i) {
            const std::string mark = "{" + std::to_string(t.slots[i]) + "}";
            const std::string with = translate(lang, (*filled)[i], depth + 1, names);
            for (size_t at = out.find(mark); at != std::string::npos; at = out.find(mark, at + with.size())) out.replace(at, mark.size(), with);
        }
        return out;
    }
    return names && depth > 0 ? (*names)(english) : english;          // a piece of a message the catalog lacks: maybe a name
}

bool isKeyOf(const json& item, const std::string& en) {
    if (!item.is_object()) return false;
    for (const char* k : {"id", "_key", "name"})
        if (const json* v = jsonFind(item, k); v && v->is_string() && v->get<std::string>() == en) return true;
    return false;
}

void mergeArray(json& base, const json& overlay) {
    const bool objects = std::any_of(overlay.begin(), overlay.end(), [](const json& v) { return v.is_object(); });
    if (!objects) {                                     // a list of plain values (names, paragraphs): the translation replaces it
        base = overlay;
        return;
    }
    std::vector<bool> taken(base.size(), false);
    for (size_t i = 0; i < overlay.size(); ++i) {
        const json& item = overlay[i];
        size_t at = base.size();
        std::string want;
        if (item.is_object()) {
            if (const json* en = jsonFind(item, "en"); en && en->is_string()) want = en->get<std::string>();
            else if (const json* id = jsonFind(item, "id"); id && id->is_string()) want = id->get<std::string>();
        }
        if (!want.empty()) {
            for (size_t b = 0; b < base.size() && at == base.size(); ++b)
                if (!taken[b] && isKeyOf(base[b], want)) at = b;
            if (at == base.size()) continue;            // what it translates is not there (any more): nothing to do
        } else if (i < base.size()) {
            at = i;
        } else {
            continue;
        }
        taken[at] = true;
        if (base[at].is_object() && item.is_object()) mergeOverlay(base[at], item);
        else if (!item.is_object()) base[at] = item;
    }
}

}  // namespace

const std::vector<std::string>& languages() {
    static const std::vector<std::string> all = {"en", "es"};
    return all;
}

std::string normalize(const std::string& lang) {
    std::string l = lowerCopy(trimmed(lang)).substr(0, 2);
    return std::ranges::contains(languages(), l) ? l : std::string(kDefault);
}

std::string nativeName(const std::string& lang) { return normalize(lang) == "es" ? "Español" : "English"; }

void loadCatalogs(const std::string& dataDir) {
    catalogs().clear();
    for (const std::string& lang : languages()) {
        if (lang == kDefault) continue;
        const std::string dir = fs::withoutTrailingSlash(dataDir) + "/system/i18n/" + lang;
        Catalog cat;
        std::vector<std::string> files = fs::listDir(dir);
        std::ranges::sort(files);
        for (const std::string& f : files) {
            if (!f.starts_with("strings") || !f.ends_with(".yaml")) continue;
            const auto root = jsonLoad(dir + "/" + f);
            if (!root) continue;
            for (auto it = root->begin(); it != root->end(); ++it) {
                if (!it.value().is_string()) continue;
                const std::string value = it.value().get<std::string>();
                Template t;
                if (parseTemplate(it.key(), t)) {
                    t.value = value;
                    cat.templates.push_back(std::move(t));
                } else {
                    cat.exact[it.key()] = value;
                }
            }
        }
        std::ranges::stable_sort(cat.templates, [](const Template& a, const Template& b) { return a.literal > b.literal; });
        catalogs()[lang] = std::move(cat);
    }
}

std::string tr(const std::string& lang, const std::string& english) { return translate(normalize(lang), english, 0, nullptr); }

std::string tr(const std::string& lang, const std::string& english, const Names& names) { return translate(normalize(lang), english, 0, &names); }

void setCurrent(const std::string& lang) { tlCurrent = normalize(lang); }
const std::string& current() { return tlCurrent; }

void mergeOverlay(json& base, const json& overlay) {
    if (!base.is_object() || !overlay.is_object()) {
        base = overlay;
        return;
    }
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        const std::string& key = it.key();
        const json& value = it.value();
        if (key == "en") continue;                      // how the item was matched, not part of it
        if (key == "name" && !base.contains("id") && !base.contains("_key") && base.contains("name")) base["_key"] = base["name"];
        if (key == "category" && !base.contains("_category_key")) base["_category_key"] = base.contains("category") ? base["category"] : json("");
        auto found = base.find(key);
        if (found == base.end() || found->is_null() || key == "fields") base[key] = value;
        else if (found->is_object() && value.is_object()) mergeOverlay(*found, value);
        else if (found->is_array() && value.is_array()) mergeArray(*found, value);
        else *found = value;
    }
}

std::optional<json> overlayFor(const std::string& dir, const std::string& file, const std::string& lang, std::string* error) {
    const std::string l = normalize(lang);
    if (l == kDefault) return std::nullopt;
    const std::string path = fs::withoutTrailingSlash(dir) + "/i18n/" + l + "/" + file;
    const auto text = fs::readFile(path);
    if (!text) return std::nullopt;
    json j;
    std::string err;
    if (!jsonParse(*text, j, &err)) {
        if (error) *error = "i18n/" + l + "/" + file + ": " + err;
        return std::nullopt;
    }
    return j;
}

bool isTextKey(const std::string& s) {
    if (s.size() <= 4 || !s.starts_with("LOC_")) return false;
    return std::ranges::all_of(s, [](unsigned char c) { return std::isupper(c) || std::isdigit(c) || c == '_'; });
}

void resolveTexts(json& doc, const TextOf& text, const TextOf& english, bool translated) {
    if (doc.is_string()) {
        if (const std::string& s = doc.get_ref<const std::string&>(); isTextKey(s)) doc = text(s);
        return;
    }
    if (doc.is_array()) {
        for (json& v : doc) resolveTexts(v, text, english, translated);
        return;
    }
    if (!doc.is_object()) return;
    // the English of a name or category that is a key, before it is replaced
    std::string englishName, englishCategory;
    bool nameKey = false, categoryKey = false;
    if (translated) {
        if (const auto it = doc.find("name"); it != doc.end() && it->is_string() && isTextKey(it->get<std::string>())) nameKey = true, englishName = english(it->get<std::string>());
        if (const auto it = doc.find("category"); it != doc.end() && it->is_string() && isTextKey(it->get<std::string>())) categoryKey = true, englishCategory = english(it->get<std::string>());
    }
    bool renamed = false;
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        resolveTexts(it.value(), text, english, translated);
        renamed |= isTextKey(it.key());
    }
    if (renamed) {                                                                   // a mapping key that is a text: the same members, in order, under their words
        json fixed = json::object();
        for (auto it = doc.begin(); it != doc.end(); ++it) fixed[isTextKey(it.key()) ? text(it.key()) : it.key()] = std::move(it.value());
        doc = std::move(fixed);
    }
    if (nameKey && !doc.contains("_key")) doc["_key"] = englishName;
    if (categoryKey && !doc.contains("_category_key")) doc["_category_key"] = englishCategory;
}

std::optional<json> loadLocalized(const std::string& dir, const std::string& file, const std::string& lang) {
    auto base = jsonLoad(fs::withoutTrailingSlash(dir) + "/" + file);
    if (!base) return std::nullopt;
    if (const auto overlay = overlayFor(dir, file, lang)) mergeOverlay(*base, *overlay);
    // the words the file keeps in the localization files next to it (<dir>/i18n/<lang>.csv, English where a language lacks one)
    Texts en, shown;
    loadTexts(dir, kDefault, en);
    if (normalize(lang) != kDefault) loadTexts(dir, lang, shown);
    if (!en.empty() || !shown.empty()) {
        const auto english = [&](const std::string& key) { const auto it = en.find(key); return it == en.end() ? key : it->second; };
        const auto text = [&](const std::string& key) {
            const auto it = shown.find(key);
            return it == shown.end() ? english(key) : it->second;
        };
        resolveTexts(*base, text, english, normalize(lang) != kDefault);
    }
    return base;
}

Texts parseCsv(const std::string& csv) {
    // the rows of the file, each a list of fields
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    size_t i = csv.starts_with("\xEF\xBB\xBF") ? 3 : 0;                        // a spreadsheet's BOM
    const auto endField = [&] {
        row.push_back(std::move(field));
        field.clear();
        any = true;
    };
    const auto endRow = [&] {
        if (any || !row.empty() || !field.empty()) {
            endField();
            rows.push_back(std::move(row));
        }
        row.clear();
        field.clear();
        any = false;
    };
    for (; i < csv.size(); ++i) {
        const char ch = csv[i];
        if (quoted) {
            if (ch == '"' && i + 1 < csv.size() && csv[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (ch == '"') {
                quoted = false;
            } else if (ch != '\r') {                                                  // (a line break inside a text is one \n, whatever Git made of the file)
                field += ch;
            }
        } else if (ch == '"' && field.empty()) {
            quoted = true;
            any = true;
        } else if (ch == ',') {
            endField();
        } else if (ch == '\n' || ch == '\r') {
            if (ch == '\r' && i + 1 < csv.size() && csv[i + 1] == '\n') ++i;
            endRow();
        } else {
            field += ch;
        }
    }
    endRow();
    Texts out;
    bool first = true;
    for (const std::vector<std::string>& r : rows) {
        const std::string key = trimmed(r.empty() ? std::string() : r[0]);
        const bool header = first && lowerCopy(key) == "key";
        first = false;
        if (header || key.empty() || key.starts_with("#")) continue;
        out[key] = r.size() > 1 ? r[1] : std::string();
    }
    return out;
}

bool loadTexts(const std::string& dir, const std::string& lang, Texts& into, std::string* error) {
    const std::string path = fs::withoutTrailingSlash(dir) + "/i18n/" + normalize(lang) + ".csv";
    if (!fs::isFile(path)) return true;
    const auto text = fs::readFile(path);
    if (!text) {
        if (error) *error = "i18n/" + normalize(lang) + ".csv could not be read";
        return false;
    }
    for (auto& [key, value] : parseCsv(*text)) into[key] = std::move(value);
    return true;
}

}  // namespace gm::i18n
