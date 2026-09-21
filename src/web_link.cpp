#include "web_link.h"

#include "jsonutil.h"

namespace gm {

std::string webLinkFor(const std::string& prefDir, const std::string& characterId) {
    const auto j = jsonLoad(fs::withoutTrailingSlash(prefDir) + "/web-access.json");
    const json* tokens = j ? jsonFind(*j, "tokens") : nullptr;
    if (!tokens) return {};
    const std::string token = jsonStr(*tokens, characterId.c_str());
    std::string base = jsonStr(*j, "base_url");
    while (base.ends_with('/')) base.pop_back();
    return token.empty() || base.empty() ? std::string() : base + "/?t=" + token;
}
}  // namespace gm
