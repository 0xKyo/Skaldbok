// The chat between the GM and the players: conversations, broadcasts, pictures, read markers, two programs writing at once.
#include <set>
#include <string>
#include <vector>

#include "fsutil.h"
#include "jsonutil.h"
#include "messages.h"
#include "packs.h"
#include "testutil.h"

using namespace gm;
using test::check;

namespace {
const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "pretend picture";
const std::string kJpg = std::string("\xFF\xD8\xFF\xE0", 4) + "pretend picture";
}  // namespace

int main() {
    const std::string root = test::scratch("chat") + "/";
    removeTree(root + "chat");
    const std::string picture = root + "map.png";
    test::write(picture, kPng);

    MessageStore store;
    store.setPrefDir(root);
    check(test::exists(root + "chat/media"), "the store makes its folders");
    check(store.thread("c-1").messages.empty() && store.unread("c-1", true) == 0, "nothing yet");

    // ---------------------------------------------------------------------------------- the GM writes
    std::string err;
    const std::vector<std::string> aria = {"c-1"}, both = {"c-1", "c-2"}, nobody;
    check(store.sendFromGm(aria, false, "", "You recognise the symbol.", "", &err), "the GM writes to one player");
    check(store.sendFromGm(both, true, "The Misty Vale party", "The bridge is out.", "", &err), "and broadcasts to the party");
    check(store.sendFromGm(aria, false, "", "", picture, &err), "a picture alone is a message");
    const Thread t1 = store.thread("c-1");
    check(t1.messages.size() == 3 && t1.messages[0].text == "You recognise the symbol." && t1.messages[0].from == "gm" && t1.messages[0].kind == "message" &&
              t1.messages[0].at.size() == 20,
          "the conversation is in order, each message from the GM with its time");
    check(t1.messages[1].kind == "broadcast" && t1.messages[1].to == "The Misty Vale party", "a broadcast is marked and says who it went to");
    check(t1.messages[2].image.ends_with(".png") && MessageStore::safeMediaName(t1.messages[2].image) && fs::isFile(store.mediaFile(t1.messages[2].image)) &&
              fs::readFile(store.mediaFile(t1.messages[2].image)).value_or("") == kPng,
          "a picture is copied into media/");
    const Thread t2 = store.thread("c-2");
    check(t2.messages.size() == 1 && t2.messages[0].id == t1.messages[1].id && t2.messages[0].kind == "broadcast", "the other player got only the broadcast, with the same id");
    check(store.thread("c-3").messages.empty(), "someone else gets nothing");
    check(!store.sendFromGm(nobody, false, "", "hi", "", &err) && !store.sendFromGm(aria, false, "", "", "", &err) && !store.sendFromGm(aria, false, "", "hi", root + "missing.png", &err),
          "no recipients, nothing to send, an unreadable picture: refused");
    test::write(root + "notes.txt", "not a picture");
    check(!store.sendFromGm(aria, false, "", "hi", root + "notes.txt", &err) && !err.empty(), "a file that is not a picture is refused by what it is, not by its name");
    check(store.thread("c-1").messages.size() == 3, "a refused message leaves no trace");
    const std::vector<std::string> sneaky = {"../evil", "media", "c-2"};
    check(store.sendFromGm(sneaky, false, "", "hi", "", &err) && !test::exists(root + "evil") && store.thread("c-2").messages.size() == 2, "unsafe ids are skipped, the others still receive it");

    // ---------------------------------------------------------------------------------- a player writes
    check(store.sendFromPlayer("c-1", "The door is trapped, I think.", "", &err), "a player writes to the GM");
    check(store.sendFromPlayer("c-1", "", kJpg, &err), "and sends a picture");
    const Thread again = store.thread("c-1");                  // (a copy: the store refreshes its own as things change)
    check(again.messages.size() == 5 && again.messages[3].from == "player" && again.messages[3].kind == "message" && again.messages[4].image.ends_with(".jpg"),
          "it lands in the same conversation, from the player");
    check(store.thread("c-2").messages.size() == 2, "and only in theirs: players do not see each other");
    check(!store.sendFromPlayer("c-1", "", "GIF-not-really", &err) && !store.sendFromPlayer("c-1", "", std::string(MessageStore::kMaxImageBytes + 1, 'x'), &err), "bytes that are not a picture, or too many: refused");
    check(!store.sendFromPlayer("c-1", std::string(MessageStore::kMaxText + 1, 'a'), "", &err) && !store.sendFromPlayer("c-1", "", "", &err), "too long, or empty: refused");
    check(!store.sendFromPlayer("../x", "hi", "", &err) && !store.sendFromPlayer("media", "hi", "", &err), "a player cannot write anywhere but their own conversation");
    check(MessageStore::imageExtension("GIF89a...") == "gif" && MessageStore::imageExtension(std::string("RIFF\0\0\0\0WEBPVP8 ", 16)) == "webp" && MessageStore::imageExtension("<svg>").empty(),
          "pictures are recognised by their first bytes (an SVG can carry script, so it is not one)");

    // ---------------------------------------------------------------------------------- unread and markers
    check(store.unread("c-1", true) == 2 && store.unread("c-1", false) == 3, "the GM has 2 unread from the player, the player 3 from the GM");
    store.markRead("c-1", true, again.messages.back().id);
    check(store.unread("c-1", true) == 0 && store.unread("c-1", false) == 3 && store.thread("c-1").gmRead == again.messages.back().id, "each side reads on its own");
    store.markRead("c-1", false, again.messages[2].id);
    check(store.unread("c-1", false) == 0, "the player reads their messages");
    store.markRead("c-1", false, again.messages[0].id);
    check(store.thread("c-1").playerRead == again.messages[2].id, "a marker only moves forward");
    std::set<std::string> ids;
    for (const Message& m : store.thread("c-1").messages) ids.insert(m.id);
    check(ids.size() == 5, "every message has its own id");

    // ---------------------------------------------------------------------------------- two programs
    MessageStore other;                                        // the web server, in the real thing
    other.setPrefDir(root);
    check(other.poll().empty() && store.poll().empty(), "the first look only takes note");
    check(other.sendFromPlayer("c-2", "Anyone seen the innkeeper?", "", &err), "the other program writes a message");
    const std::vector<std::string> seen = store.poll();
    check(seen == std::vector<std::string>{"c-2"} && store.thread("c-2").messages.size() == 3 && store.thread("c-2").messages.back().from == "player", "this one notices which conversation changed, and reads it");
    check(store.poll().empty(), "and nothing changes until something does");
    store.markRead("c-2", true, store.thread("c-2").messages.back().id);
    check(store.poll() == std::vector<std::string>{"c-2"}, "even a read marker counts as a change (the other side can show 'seen')");

    // ---------------------------------------------------------------------------------- files and limits
    test::write(root + "chat/c-4/m-000000000001-aaaaaa.json", "{\"id\":\"m-000000000001-aaaaaa\",\"from\":\"gm\",\"kind\":\"broadcast\",\"text\":\"x\",\"image\":\"../../secret.png\"}");
    test::write(root + "chat/c-4/m-000000000002-bbbbbb.json", "{ half a file");
    test::write(root + "chat/c-4/m-000000000003-cccccc.json", "{\"id\":\"m-000000000003-cccccc\",\"from\":\"player\",\"kind\":\"broadcast\",\"text\":\"y\"}");
    const Thread hand = store.thread("c-4");
    check(hand.messages.size() == 2 && hand.messages[0].image.empty() && hand.messages[0].kind == "broadcast" && hand.messages[1].kind == "message",
          "a hand-edited conversation cannot point outside media/, a damaged file is skipped, and only the GM can broadcast");
    check(!MessageStore::safeMediaName("../a.png") && !MessageStore::safeMediaName("a.svg") && !MessageStore::safeMediaName("dir/a.png") && MessageStore::safeMediaName("m-1-2.webp"), "media names: a safe id plus a picture extension");

    for (size_t i = 0; i < MessageStore::kKeep + 10; ++i) store.sendFromPlayer("c-5", "message " + std::to_string(i), "", &err);
    const Thread big = store.thread("c-5");
    check(big.messages.size() == MessageStore::kKeep && big.messages.back().text == "message " + std::to_string(MessageStore::kKeep + 9) && big.messages.front().text == "message 10",
          "a conversation keeps the newest messages only");

    const std::string kept = store.thread("c-1").messages[2].image;
    store.forget("c-2");
    check(store.thread("c-2").messages.empty() && fs::isFile(store.mediaFile(kept)), "forgetting a character removes their conversation, not other people's pictures");
    store.forget("c-1");
    check(!fs::isFile(store.mediaFile(kept)) && store.thread("c-1").messages.empty(), "and the pictures nobody points to any more");

    return test::finish();
}
