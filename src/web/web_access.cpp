#include "web/web_access.h"

#include <cstdio>
#include <random>

#include "parsing/jsonutil.h"

namespace gm {

WebAccess::WebAccess(std::string prefsDir, std::string publicUrl) : publicUrl_(std::move(publicUrl)) {
    file_ = fs::withoutTrailingSlash(std::move(prefsDir)) + "/" + fileName();
    read();
}

namespace {
constexpr size_t kTokenLength = 8;
}

// A token made only of digits (or digits, one "e" and digits) would be written to the yaml file as a number and read back as one, so the
// token would be lost: those are not used.
static bool readsAsANumber(const std::string& t) {
    const size_t e = t.find('e');
    const auto digits = [](const std::string& s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; };
    return digits(t) || (e != std::string::npos && digits(t.substr(0, e)) && digits(t.substr(e + 1)));
}

std::string WebAccess::newToken() {
    std::random_device rd;                                       // the operating system's entropy source
    char buf[9];
    do {
        std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(rd()));
    } while (readsAsANumber(buf));
    return buf;
}

void WebAccess::read() {
    tokens_.clear();
    fileUrl_.clear();
    gmToken_.clear();
    if (const auto j = jsonLoad(file_)) {
        if (const json* t = jsonFind(*j, "tokens"); t && t->is_object())
            for (auto it = t->begin(); it != t->end(); ++it)
                if (it.value().is_string()) tokens_[it.key()] = it.value().get<std::string>();
        fileUrl_ = jsonStr(*j, "base_url");
        gmToken_ = jsonStr(*j, "gm_token");
    }
    index();
}
void WebAccess::index() {
    byToken_.clear();
    for (const auto& [id, token] : tokens_) byToken_[token] = id;
}

void WebAccess::write() {
    json j;
    j["format"] = 1;
    j["base_url"] = publicUrl_;
    j["gm_token"] = gmToken_;
    json tokens = json::object();
    for (const auto& [id, token] : tokens_) tokens[id] = token;
    j["tokens"] = tokens;
    fs::writeFile(file_, jsonToYaml(j));
    fileUrl_ = publicUrl_;
}

bool WebAccess::sync(const std::vector<std::string>& readable, const std::set<std::string>& existing) {
    bool changed = false;
    if (gmToken_.size() != kTokenLength) {      // empty, or from the days of 32-digit tokens
        gmToken_ = newToken();
        changed = true;
    }
    for (auto& [id, token] : tokens_)
        if (token.size() != kTokenLength) {
            token = newToken();
            changed = true;
        }
    for (const std::string& id : readable)
        if (!tokens_.count(id)) {
            tokens_[id] = newToken();
            changed = true;
        }
    for (auto it = tokens_.begin(); it != tokens_.end();) {
        if (existing.count(it->first)) {
            ++it;
        } else {
            it = tokens_.erase(it);
            changed = true;
        }
    }
    if (!changed && fileUrl_ == publicUrl_) return false;
    index();
    write();
    return true;
}

std::string WebAccess::regenerate(const std::string& characterId) {
    tokens_[characterId] = newToken();
    index();
    write();
    return tokens_[characterId];
}

std::string WebAccess::characterFor(const std::string& token) const {
    if (token.size() < 8 || token.size() > 128) return {};
    auto it = byToken_.find(token);
    return it == byToken_.end() ? std::string() : it->second;
}

std::string WebAccess::linkFor(const std::string& characterId) const {
    auto it = tokens_.find(characterId);
    return it == tokens_.end() ? std::string() : publicUrl_ + "/?t=" + it->second;
}

bool WebAccess::isGm(const std::string& token) const {
    return !gmToken_.empty() && token.size() >= 8 && token == gmToken_;
}

std::string WebAccess::gmLink() const {
    return gmToken_.empty() ? std::string() : publicUrl_ + "/?gm=" + gmToken_;
}

}  // namespace gm
