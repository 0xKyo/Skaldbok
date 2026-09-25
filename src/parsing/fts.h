// Helpers shared by the two full-text indexes (the book database and the content packs).
#pragma once

#include <cctype>
#include <string>

namespace gm {

// Every word becomes a prefix term: "cent att" finds the centaur's attacks while typing. Empty if nothing searchable.
inline std::string ftsMatch(const std::string& userText) {
    std::string match, word;
    auto flush = [&] {
        if (word.empty()) return;
        if (!match.empty()) match += ' ';
        match += '"' + word + "\"*";
        word.clear();
    };
    for (unsigned char c : userText) {
        if (std::isalnum(c) || c >= 0x80) word += static_cast<char>(c);
        else flush();
    }
    flush();
    return match;
}

inline std::string trimmed(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

inline std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace gm
