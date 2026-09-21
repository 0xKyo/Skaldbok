// The GM app shows each player's personal web link. The links are made by the web server (skaldbok_web), which keeps
// them in web-access.json in the per-user folder; the GM app only reads that file.
#pragma once

#include <string>

namespace gm {

// "http://players.example/?t=<token>", or an empty string if the web server has not made a link for that character yet.
std::string webLinkFor(const std::string& prefDir, const std::string& characterId);

}  // namespace gm
