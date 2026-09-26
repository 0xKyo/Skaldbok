// Who may see which character. Each character gets a random secret token; the player's personal link carries it.
//
// The tokens live in web-access.json next to the GM app's files. The web server is its only writer; the GM app reads it to
// show ready-made links.
#pragma once

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace gm {

class WebAccess {
public:
    WebAccess(std::string prefsDir, std::string publicUrl);

    // Gives every readable character a token and forgets the tokens of characters whose file is gone. A file that exists
    // but cannot be read right now (the GM app is in the middle of saving it) keeps its token. True if the file changed.
    bool sync(const std::vector<std::string>& readable, const std::set<std::string>& existing);
    std::string regenerate(const std::string& characterId);   // the old link stops working at once

    std::string characterFor(const std::string& token) const;  // "" if the token is not one of ours
    std::string linkFor(const std::string& characterId) const; // "" if the character has no token
    const std::map<std::string, std::string>& tokens() const { return tokens_; }

    static std::string newToken();                              // 128 random bits, as 32 hex digits
    static std::string fileName() { return "web-access.yaml"; }

private:
    void read();
    void write();
    void index();

    std::string file_, publicUrl_, fileUrl_;
    std::map<std::string, std::string> tokens_;                // character id -> token
    std::unordered_map<std::string, std::string> byToken_;      // token -> character id
};

}  // namespace gm
