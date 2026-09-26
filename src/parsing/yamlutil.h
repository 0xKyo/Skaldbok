// YAML ↔ nlohmann::ordered_json bridge. All file I/O uses YAML; the in-memory model stays nlohmann::ordered_json.
#pragma once

#include <optional>
#include <string>

#include <yaml-cpp/yaml.h>

#include "json.hpp"
#include "parsing/fsutil.h"

namespace gm {

using json = nlohmann::ordered_json;

// Convert a yaml-cpp node tree to nlohmann::ordered_json, preserving document key order.
inline json yamlNodeToJson(const YAML::Node& node) {
    switch (node.Type()) {
        case YAML::NodeType::Null:
            return nullptr;
        case YAML::NodeType::Scalar: {
            const std::string tag = node.Tag();
            if (tag == "tag:yaml.org,2002:null") return nullptr;
            if (tag == "tag:yaml.org,2002:bool") return node.as<bool>();
            if (tag == "tag:yaml.org,2002:int") {
                try { return node.as<int64_t>(); } catch (...) {}
            }
            if (tag == "tag:yaml.org,2002:float") {
                try { return node.as<double>(); } catch (...) {}
            }
            if (tag == "tag:yaml.org,2002:str") return node.Scalar();
            // Untagged scalar: infer type from value
            const std::string& s = node.Scalar();
            if (s.empty()) return s;
            if (s == "null" || s == "~") return nullptr;
            if (s == "true") return true;
            if (s == "false") return false;
            // Try integer
            try {
                size_t pos;
                const long long iv = std::stoll(s, &pos);
                if (pos == s.size()) return iv;
            } catch (...) {}
            // Try float
            try {
                size_t pos;
                const double dv = std::stod(s, &pos);
                if (pos == s.size()) return dv;
            } catch (...) {}
            return s;
        }
        case YAML::NodeType::Sequence: {
            json arr = json::array();
            for (const auto& item : node) arr.push_back(yamlNodeToJson(item));
            return arr;
        }
        case YAML::NodeType::Map: {
            json obj = json::object();
            for (const auto& kv : node) obj[kv.first.as<std::string>()] = yamlNodeToJson(kv.second);
            return obj;
        }
        default:
            return nullptr;
    }
}

// Emit a single nlohmann::json value to a yaml-cpp Emitter, using block style and literal strings.
inline void emitJson(YAML::Emitter& out, const json& j) {
    if (j.is_null()) {
        out << YAML::Null;
    } else if (j.is_boolean()) {
        out << j.get<bool>();
    } else if (j.is_number_integer()) {
        out << j.get<int64_t>();
    } else if (j.is_number_unsigned()) {
        out << j.get<uint64_t>();
    } else if (j.is_number_float()) {
        out << j.get<double>();
    } else if (j.is_string()) {
        const std::string& s = j.get<std::string>();
        if (s.find('\n') != std::string::npos)
            out << YAML::Literal << s;
        else
            out << s;
    } else if (j.is_array()) {
        out << YAML::BeginSeq;
        for (const auto& el : j) emitJson(out, el);
        out << YAML::EndSeq;
    } else if (j.is_object()) {
        out << YAML::BeginMap;
        for (auto it = j.begin(); it != j.end(); ++it) {
            out << YAML::Key << it.key() << YAML::Value;
            emitJson(out, it.value());
        }
        out << YAML::EndMap;
    }
}

// Serialize a json value to a YAML string. Multi-line text values use literal block style (|).
inline std::string jsonToYaml(const json& j) {
    YAML::Emitter out;
    emitJson(out, j);
    return std::string(out.c_str()) + "\n";
}

// Parse YAML (or JSON, which is valid YAML) text; on failure fills *error and returns false.
inline bool yamlParse(const std::string& text, json& out, std::string* error) {
    try {
        YAML::Node root = YAML::Load(text);
        out = yamlNodeToJson(root);
        return true;
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

// Read a file and parse it as YAML; nothing if missing, damaged, or not a mapping.
inline std::optional<json> yamlLoad(const std::string& path) {
    const auto text = fs::readFile(path);
    json j;
    if (!text || !yamlParse(*text, j, nullptr) || !j.is_object()) return std::nullopt;
    return j;
}

}  // namespace gm
