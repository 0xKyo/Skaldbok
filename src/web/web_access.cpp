#include "web/web_access.h"

#include <cstdio>
#include <random>

#include "parsing/jsonutil.h"

namespace gm {

WebAccess::WebAccess(std::string prefsDir, std::string publicUrl) : publicUrl_(std::move(publicUrl)) {
    file_ = fs::withoutTrailingSlash(std::move(prefsDir)) + "/" + fileName();
    read();
}

std::string WebAccess::newToken() {
    std::random_device rd;                                       // the operating system's entropy source
    std::string out;
    for (int i = 0; i < 4; ++i) {
        char buf[9];
        std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(rd()));
        out += buf;
    }
    return out;
}

void WebAccess::read() {
    tokens_.clear();
    fileUrl_.clear();
    if (const auto j = jsonLoad(file_)) {
        if (const json* t = jsonFind(*j, "tokens"); t && t->is_object())
            for (auto it = t->begin(); it != t->end(); ++it)
                if (it.value().is_string()) tokens_[it.key()] = it.value().get<std::string>();
        fileUrl_ = jsonStr(*j, "base_url");
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
    json tokens = json::object();
    for (const auto& [id, token] : tokens_) tokens[id] = token;
    j["tokens"] = tokens;
    fs::writeFile(file_, j.dump(2) + "\n");
    fileUrl_ = publicUrl_;
}

bool WebAccess::sync(const std::vector<std::string>& readable, const std::set<std::string>& existing) {
    bool changed = false;
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
    if (token.size() < 16 || token.size() > 128) return {};
    auto it = byToken_.find(token);
    return it == byToken_.end() ? std::string() : it->second;
}

std::string WebAccess::linkFor(const std::string& characterId) const {
    auto it = tokens_.find(characterId);
    return it == tokens_.end() ? std::string() : publicUrl_ + "/?t=" + it->second;
}

}  // namespace gm
