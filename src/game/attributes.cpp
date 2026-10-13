#include "game/attributes.h"

#include <algorithm>
#include <cstdlib>

#include "parsing/fts.h"

namespace gm {
namespace {

// "base-chance" -> "BASE_CHANCE": the text of a key made from an id when the file writes none.
std::string upperId(const std::string& id) {
    std::string out;
    for (char c : id) out += (c == '-' || c == ' ') ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 40) return false;
    return std::ranges::all_of(id, [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '-' || c == '_'; });
}

std::string keyOr(const json& o, const char* field, const std::string& otherwise) {
    const std::string own = trimmed(jsonStr(o, field));
    return own.empty() ? otherwise : own;
}

}  // namespace

void AttributeSet::addAttributes(const json& list, std::vector<std::string>& warnings) {
    if (!list.is_array()) {
        warnings.push_back("attributes: expected a list");
        return;
    }
    int index = 0;
    for (const json& o : list) {
        ++index;
        const std::string id = o.is_object() ? trimmed(jsonStr(o, "id")) : std::string();
        if (!validId(id)) {
            warnings.push_back("attributes: #" + std::to_string(index) + " needs an \"id\" (lowercase letters, digits, - or _), skipped");
            continue;
        }
        AttributeDef d;
        d.id = id;
        d.aliases = jsonStrings(o, "aliases");
        const std::string base = "LOC_ATTRIBUTE_" + upperId(id);
        d.nameKey = keyOr(o, "name", base);
        d.abbrKey = keyOr(o, "abbr", base + "_ABBR");
        d.descriptionKey = keyOr(o, "description", base + "_DESC");
        const auto same = std::ranges::find(attributes_, id, &AttributeDef::id);
        if (same != attributes_.end()) *same = std::move(d);
        else attributes_.push_back(std::move(d));
    }
}

void AttributeSet::addLookups(const json& list, std::vector<std::string>& warnings) {
    if (!list.is_array()) {
        warnings.push_back("lookups: expected a list");
        return;
    }
    int index = 0;
    for (const json& o : list) {
        ++index;
        const std::string id = o.is_object() ? trimmed(jsonStr(o, "id")) : std::string();
        if (!validId(id)) {
            warnings.push_back("lookups: #" + std::to_string(index) + " needs an \"id\" (lowercase letters, digits, - or _), skipped");
            continue;
        }
        Lookup l;
        l.id = id;
        l.attributes = jsonStrings(o, "attributes");
        l.fallback = o.contains("default") ? jsonText(o["default"]) : std::string();
        l.format = lowerCopy(jsonStr(o, "format", "number"));
        const std::string base = "LOC_LOOKUP_" + upperId(id);
        l.titleKey = keyOr(o, "title", base);
        l.rangeKey = keyOr(o, "range", base + "_RANGE");
        l.valueKey = keyOr(o, "values", base + "_VALUE");
        if (const json* rows = jsonFind(o, "rows"); rows && rows->is_array())
            for (const json& r : *rows) {
                if (!r.is_object()) continue;
                LookupRow row;
                if (const json* f = jsonFind(r, "from"); f && f->is_number_integer()) row.hasFrom = true, row.from = f->get<int>();
                if (const json* t = jsonFind(r, "to"); t && t->is_number_integer()) row.hasTo = true, row.to = t->get<int>();
                row.value = r.contains("value") ? jsonText(r["value"]) : std::string();
                row.labelKey = trimmed(jsonStr(r, "label"));
                l.rows.push_back(std::move(row));
            }
        const auto same = std::ranges::find(lookups_, id, &Lookup::id);
        if (same != lookups_.end()) *same = std::move(l);
        else lookups_.push_back(std::move(l));
    }
}

const AttributeDef* AttributeSet::find(const std::string& idOrAlias) const {
    const std::string want = lowerCopy(trimmed(idOrAlias));
    if (want.empty()) return nullptr;
    for (const AttributeDef& d : attributes_)
        if (d.id == want) return &d;
    for (const AttributeDef& d : attributes_)
        for (const std::string& a : d.aliases)
            if (lowerCopy(a) == want) return &d;
    return nullptr;
}

int AttributeSet::indexOf(const std::string& idOrAlias) const {
    const AttributeDef* d = find(idOrAlias);
    return d ? static_cast<int>(d - attributes_.data()) : -1;
}

std::string AttributeSet::idOf(const std::string& idOrAlias) const {
    const AttributeDef* d = find(idOrAlias);
    return d ? d->id : std::string();
}

const Lookup* AttributeSet::lookup(const std::string& id) const {
    const auto it = std::ranges::find(lookups_, id, &Lookup::id);
    return it == lookups_.end() ? nullptr : &*it;
}

std::string AttributeSet::value(const std::string& lookupId, int score) const {
    const Lookup* l = lookup(lookupId);
    if (!l) return std::string();
    for (const LookupRow& r : l->rows)
        if (r.covers(score)) return r.value;
    return l->fallback;
}

int AttributeSet::number(const std::string& lookupId, int score, int otherwise) const {
    const std::string v = trimmed(value(lookupId, score));
    if (v.empty()) return otherwise;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    return end != v.c_str() && *end == '\0' ? static_cast<int>(n) : otherwise;
}

}  // namespace gm
