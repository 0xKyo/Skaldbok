// Languages. English is the language of the data and of the program: every id, key and saved file is in English, and a pack can
// carry a translation of its files next to them (<pack>/i18n/<lang>/<file>.yaml, docs/HOMEBREW.md) that is laid over the English
// when the content is read in that language. The program's own words (labels, messages) are translated by a catalog,
// data/system/i18n/<lang>/strings.yaml: "English text": "translation".
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "parsing/jsonutil.h"

namespace gm::i18n {

inline constexpr const char* kDefault = "en";

const std::vector<std::string>& languages();          // "en", "es"
// A language the app has ("es", "es-AR" -> "es"); anything else is English.
std::string normalize(const std::string& lang);
// The name of a language in that language ("English", "Español").
std::string nativeName(const std::string& lang);

// Reads every catalog under <dataDir>/system/i18n (again: the latest call wins). Without it, tr() gives the English back.
void loadCatalogs(const std::string& dataDir);
// The translation of a text of the program. A catalog key may hold placeholders, "{0}", "{1}"...: "Could not send: {0}." then
// translates any text of that shape, and what fills a placeholder is translated in turn when the catalog has it. A text the
// catalog lacks stays in English.
std::string tr(const std::string& lang, const std::string& english);
// The same, where what fills a placeholder may also be a name of the content ("Awareness at 19 is above..."): `names` says how it is
// shown (it gives the text back when it is no name it knows).
std::string tr(const std::string& lang, const std::string& english, const std::function<std::string(const std::string&)>& names);

// The language of what is being done now on this thread (the request being answered, the language picked on the desktop).
void setCurrent(const std::string& lang);
const std::string& current();
inline std::string tr(const std::string& english) { return tr(current(), english); }

// Sets the current language for as long as it lives.
class Scope {
public:
    explicit Scope(const std::string& lang) : before_(current()) { setCurrent(lang); }
    ~Scope() { setCurrent(before_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    std::string before_;
};

// Lays a translation over a data file's content (see docs/HOMEBREW.md, "Translations"). Objects merge key by key; a list of objects
// is matched item by item, by "id" or by the English "name" written as "en" when the item has one, else by position; any other
// value, a list of plain values and every "fields" object replace what was there. Whatever it renames keeps its English: a
// replaced "name" is kept as "_key" (what the item's key is made of) and a replaced "category" as "_category_key".
void mergeOverlay(json& base, const json& overlay);
// The overlay of <dir>/<file> in that language, <dir>/i18n/<lang>/<file>; nothing for English or when there is none.
std::optional<json> overlayFor(const std::string& dir, const std::string& file, const std::string& lang, std::string* error = nullptr);
// <dir>/<file> read in that language: the file with its overlay laid over it. Nothing if the file is missing or damaged.
std::optional<json> loadLocalized(const std::string& dir, const std::string& file, const std::string& lang);

// ---- texts by key (the localization files)
// Data files do not hold words, only keys: `name: LOC_SKILL_BUSHCRAFT`. The words live in one CSV per language next to the data,
// <dir>/i18n/<lang>.csv (data/system/i18n/es.csv, <pack>/i18n/es.csv), one text per row: `key,text`. Anyone can open it in a
// spreadsheet and add a language by copying en.csv. The first row may be a header (`key,text`); more columns are ignored (notes).
using Texts = std::map<std::string, std::string>;
// Reads CSV text (RFC 4180: quoted fields, "" for a quote, line breaks inside quotes, CRLF, a BOM) into key -> text. A later row
// with the same key wins. Rows without a key are skipped.
Texts parseCsv(const std::string& csv);
// Adds the texts of <dir>/i18n/<lang>.csv to `into`, over what is there. False (and `error`) only if the file exists and cannot be read.
bool loadTexts(const std::string& dir, const std::string& lang, Texts& into, std::string* error = nullptr);

// A key as the data files write it: LOC_ and capital letters, digits and _ (LOC_SKILL_BUSHCRAFT_DESC).
bool isTextKey(const std::string& s);
using TextOf = std::function<std::string(const std::string&)>;
// Replaces, anywhere in a data file's content, every key by its text: a value (`name: LOC_SKILL_BUSHCRAFT`) and a mapping key
// (`fields: {LOC_FIELD_SIZE: ...}`). A `name` or `category` that was a key keeps its English text as `_key` / `_category_key` when it
// is shown in another language, as the overlays do (so keys, links and "rollable" go on knowing the item by its English name).
// `text` gives a key's text in the language shown, `english` in English.
void resolveTexts(json& doc, const TextOf& text, const TextOf& english, bool translated);

}  // namespace gm::i18n
