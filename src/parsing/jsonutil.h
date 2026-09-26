// Data-file reading helpers. Files are YAML; the in-memory representation is nlohmann::ordered_json.
// Every accessor is forgiving: a missing key or wrong type gives an empty/default value instead of an exception.
#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "parsing/yamlutil.h"  // declares gm::json, yamlParse, yamlLoad, jsonToYaml

namespace gm {

// Text of a value: strings as they are, numbers and booleans printed, arrays of strings joined by newlines
// (so a long description can be written as a list of paragraphs).
inline std::string jsonText(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number() || v.is_boolean()) return v.dump();
    if (v.is_array()) {
        std::string out;
        for (const json& e : v) {
            const std::string t = jsonText(e);
            if (t.empty()) continue;
            if (!out.empty()) out += '\n';
            out += t;
        }
        return out;
    }
    return {};
}

inline const json* jsonFind(const json& o, const char* key) {
    if (!o.is_object()) return nullptr;
    auto it = o.find(key);
    return it == o.end() || it->is_null() ? nullptr : &*it;
}

inline std::string jsonStr(const json& o, const char* key, const std::string& def = {}) {
    const json* v = jsonFind(o, key);
    return v ? jsonText(*v) : def;
}

inline int jsonInt(const json& o, const char* key, int def = 0) {
    const json* v = jsonFind(o, key);
    if (!v) return def;
    if (v->is_number()) return static_cast<int>(v->get<double>());
    if (v->is_string()) {
        const std::string s = v->get<std::string>();
        return s.empty() ? def : std::atoi(s.c_str());
    }
    return def;
}

inline bool jsonBool(const json& o, const char* key, bool def = false) {
    const json* v = jsonFind(o, key);
    if (!v) return def;
    if (v->is_boolean()) return v->get<bool>();
    if (v->is_number()) return v->get<double>() != 0.0;
    if (v->is_string()) {
        const std::string s = v->get<std::string>();
        return s == "true" || s == "yes" || s == "1";
    }
    return def;
}

// An array of strings (numbers are converted); a single string becomes a one-element list.
inline std::vector<std::string> jsonStrings(const json& o, const char* key) {
    std::vector<std::string> out;
    const json* v = jsonFind(o, key);
    if (!v) return out;
    if (v->is_array()) {
        for (const json& e : *v) {
            const std::string t = jsonText(e);
            if (!t.empty()) out.push_back(t);
        }
    } else {
        const std::string t = jsonText(*v);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

// Parses YAML (or JSON) text; on failure returns false and says where.
inline bool jsonParse(const std::string& text, json& out, std::string* error) {
    // Strip a UTF-8 byte-order mark that some Windows editors add.
    const std::string* src = &text;
    std::string stripped;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        stripped = text.substr(3);
        src = &stripped;
    }
    return yamlParse(*src, out, error);
}

// Reads a file and parses it as YAML; nothing if missing, damaged, or not a mapping.
inline std::optional<json> jsonLoad(const std::string& path) {
    return yamlLoad(path);
}

}  // namespace gm
