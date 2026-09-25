// The chat between the GM and each player: one conversation per character, always with the GM (players never write to each other).
// The GM can also broadcast to a party or to everyone; a broadcast lands in each player's conversation, marked as one.
//
// Written by two programs at once (the app and the web server), so nothing is ever rewritten: every message is its own file
// (chat/<character id>/<message id>.json, ids sort by time) and each side keeps only a small "read up to" marker. Pictures live in
// chat/media/. No UI in here.
#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gm {

struct Message {
    std::string id;
    std::string at;                       // ISO 8601, UTC
    std::string from;                     // "gm" or "player"
    std::string kind;                     // "message" or "broadcast" (always from the GM)
    std::string to;                       // a broadcast's addressee as the GM saw it: "The Misty Vale party", "Everyone"
    std::string text;
    std::string image;                    // file name inside chat/media/, empty if none
};

struct Thread {
    std::vector<Message> messages;        // oldest first
    std::string gmRead, playerRead;       // the last message each side has seen
};

class MessageStore {
public:
    static constexpr size_t kKeep = 300;                     // messages kept per conversation
    static constexpr size_t kMaxImageBytes = 8u << 20;
    static constexpr size_t kMaxText = 4000;

    void setPrefDir(const std::string& prefDir);             // "<prefDir>chat/"
    // How long a conversation read from disk is trusted before the folder is looked at again (the app draws every frame; the
    // server, which is asked once per request, keeps 0). Writes made through this store always show at once.
    void setFreshMs(unsigned ms) { freshMs_ = ms; }

    // The GM writes to some characters (a broadcast when `broadcast`, with `to` naming who it went to). The picture is a file.
    bool sendFromGm(std::span<const std::string> characterIds, bool broadcast, const std::string& to, const std::string& text,
                    const std::string& imagePath, std::string* error);
    // A player writes to the GM. The picture, if any, is the bytes of a PNG, JPEG, GIF or WebP (judged by the bytes, not a name).
    bool sendFromPlayer(const std::string& characterId, const std::string& text, std::string_view imageBytes, std::string* error);

    const Thread& thread(const std::string& characterId) const;      // (kept until the folder changes)
    void markRead(const std::string& characterId, bool byGm, const std::string& upToId);
    int unread(const std::string& characterId, bool forGm) const;    // messages from the other side after the marker
    void forget(const std::string& characterId);                     // a deleted character's conversation goes too

    // The characters whose conversation changed (from any program) since the last call. The first call only takes note.
    std::vector<std::string> poll();

    std::string mediaFile(const std::string& fileName) const { return dir_ + "/media/" + fileName; }
    static bool safeMediaName(const std::string& name);              // what may be a file name inside media/
    static std::string imageExtension(std::string_view bytes);       // png, jpg, gif or webp; "" if these bytes are not a picture

private:
    struct Cached {
        size_t signature = 0;
        unsigned long long checkedAt = 0;
        Thread thread;
    };

    std::string folder(const std::string& characterId) const { return dir_ + "/" + characterId; }
    bool put(const std::string& characterId, const Message& m, std::string* error) const;
    bool saveImage(const std::string& id, std::string_view bytes, std::string& fileName, std::string* error) const;
    void trim(const std::string& characterId) const;
    void pruneMedia() const;
    // What a conversation is made of right now: the names of its messages and what its read markers say. File dates would do, but
    // they are only as fine as the system clock tick (about 15 ms), and two changes can land in one tick.
    size_t signature(const std::string& characterId) const;

    std::string dir_;
    mutable std::map<std::string, Cached> cache_;
    std::map<std::string, size_t> signatures_;
    bool primed_ = false;
    unsigned freshMs_ = 0;
};

}  // namespace gm
