// The players' web server: authentication, what a player is shown (and what not), live updates, tokens, and real HTTP.
// Ported from the first Express version of the server; the expectations are the same. The fixtures are files written by
// the real GM app (tests/fixtures/web/prefs) and a tiny invented stand-in for the Core pack (the two books and a few cards), so this runs without the books.
#include <algorithm>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <httplib.h>

#include "fsutil.h"
#include "jsonutil.h"
#include "changelog.h"
#include "messages.h"
#include "sheet_edit.h"
#include "packs.h"
#include "testutil.h"
#include "web/web_app.h"
#include "web_link.h"
#include "web/web_server.h"

using namespace gm;
using test::check;

namespace {

// ---------------------------------------------------------------------------------------------- setup

void copyTree(const std::string& from, const std::string& to) {
    SDL_CreateDirectory(to.c_str());
    std::vector<std::string> names;
    SDL_EnumerateDirectory(
        from.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<std::vector<std::string>*>(user)->push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &names);
    for (const std::string& n : names) {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo((from + "/" + n).c_str(), &info)) continue;
        if (info.type == SDL_PATHTYPE_DIRECTORY) copyTree(from + "/" + n, to + "/" + n);
        else SDL_CopyFile((from + "/" + n).c_str(), (to + "/" + n).c_str());
    }
}

struct Env {
    std::string root, prefs, data;
    WebConfig config;
};

Env makeEnv(const std::string& name, bool realData = false) {
    Env e;
    e.root = test::scratch("web-" + name);
    removeTree(e.root);
    SDL_CreateDirectory(e.root.c_str());
    const std::string fixtures = test::sourceDir() + "/tests/fixtures/web";
    e.prefs = e.root + "/prefs";
    copyTree(fixtures + "/prefs", e.prefs);
    copyTree(test::sourceDir() + "/examples/frostmarch-tales", e.prefs + "/packs/frostmarch");
    test::write(e.prefs + "/packs/hidden-pack/manifest.json", "{\"format\":1,\"id\":\"hidden-pack\",\"name\":\"Hidden\"}");
    test::write(e.prefs + "/packs/hidden-pack/spells.json", "{\"spells\":[{\"name\":\"Hidden Spell\"}]}");
    if (realData) {
        e.data = SKALDBOK_DEV_DATA_DIR;
    } else {
        e.data = e.root + "/data";
        copyTree(fixtures + "/data", e.data);                 // a tiny Core: the two books and a few cards
    }
    e.config.port = 0;
    e.config.host = "127.0.0.1";
    e.config.prefsDir = e.prefs;
    e.config.dataDir = e.data;
    e.config.publicUrl = "http://players.test";
    e.config.staticDir = e.root + "/no-client";
    e.config.maxFailures = 100;
    e.config.failureWindowMs = 60000;
    e.config.refreshMs = 0;                       // look at the files on every request, so an edit is seen at once
    return e;
}

std::string base64(const std::string& in) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned buffer = 0;
    int bits = 0;
    for (unsigned char c : in) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += alphabet[(buffer >> bits) & 63];
        }
    }
    if (bits > 0) out += alphabet[(buffer << (6 - bits)) & 63];
    while (out.size() % 4) out += '=';
    return out;
}

const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "pretend picture";

// A player's request, straight into the app.
WebResponse get(WebApp& app, std::string path, const std::string& token = "", const std::string& ip = "10.0.0.1", const std::string& method = "GET", const std::string& body = "") {
    WebRequest r;
    r.method = method;
    r.body = body;
    r.ip = ip;
    const size_t q = path.find('?');
    if (q != std::string::npos) {
        std::string query = path.substr(q + 1);
        path = path.substr(0, q);
        size_t pos = 0;
        while (pos < query.size()) {
            size_t amp = query.find('&', pos);
            if (amp == std::string::npos) amp = query.size();
            const std::string pair = query.substr(pos, amp - pos);
            const size_t eq = pair.find('=');
            r.query[pair.substr(0, eq)] = eq == std::string::npos ? "" : pair.substr(eq + 1);
            pos = amp + 1;
        }
    }
    r.path = path;
    if (!token.empty()) r.headers["authorization"] = "Bearer " + token;
    return app.handle(r);
}

json body(const WebResponse& r) {
    json j;
    jsonParse(r.body, j, nullptr);
    return j;
}

const Character* byName(WebApp& app, const std::string& name) {
    for (const Character& c : app.characters())
        if (c.name == name) return app.findCharacter(c.id);
    return nullptr;
}

std::string tokenOf(WebApp& app, const std::string& name) {
    const Character* c = byName(app, name);
    return c ? app.access().tokens().at(c->id) : std::string();
}

std::string charFile(const Env& e, WebApp& app, const std::string& name) { return e.prefs + "/characters/" + byName(app, name)->id + ".json"; }

json readJson(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    json j;
    if (data) jsonParse(std::string(static_cast<const char*>(data), size), j, nullptr);
    SDL_free(data);
    return j;
}

std::string readText(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    std::string s = data ? std::string(static_cast<const char*>(data), size) : std::string();
    SDL_free(data);
    return s;
}

bool contains(const std::string& hay, const std::string& needle) { return hay.contains(needle); }

const json* find(const json& array, const char* key, const std::string& value) {
    for (const json& e : array)
        if (jsonStr(e, key) == value) return &e;
    return nullptr;
}

// ----------------------------------------------------------------------------------------------- tests

void authentication() {
    Env e = makeEnv("auth");
    WebApp app(e.config);
    check(app.ok(), "the app starts on the fixtures: " + app.error());
    const std::string brenna = tokenOf(app, "Brenna");
    check(get(app, "/api/health").status == 200, "health needs no token");
    check(get(app, "/api/me").status == 401 && get(app, "/api/party").status == 401 && get(app, "/api/content").status == 401, "without a token every route is refused");
    check(get(app, "/api/me", "not-a-real-token-at-all-1234").status == 401, "a wrong token is refused");
    WebRequest basic;
    basic.path = "/api/me";
    basic.headers["authorization"] = "Basic " + brenna;
    check(app.handle(basic).status == 401, "only Bearer tokens count");
    WebRequest viaQuery;
    viaQuery.path = "/api/me";
    viaQuery.query["t"] = brenna;
    check(app.handle(viaQuery).status == 401, "a token in the URL is not accepted (it would end up in logs)");
    const WebResponse refused = get(app, "/api/me", "short");
    check(!contains(refused.body, brenna) && contains(refused.body, "personal link"), "the error says nothing about why");
    check(get(app, "/api/me", brenna, "10.0.0.1", "POST").status == 405, "a valid token cannot write: the API is read-only");
}

void sheetView() {
    Env e = makeEnv("sheet");
    WebApp app(e.config);
    const json me = body(get(app, "/api/me", tokenOf(app, "Brenna")));
    check(me["name"] == "Brenna" && me["nickname"] == "Grimjaw" && me["player"] == "Sebastián", "name, nickname and player");
    check(me["kin"]["name"] == "Human" && me["profession"]["name"] == "Fighter" && me["age"]["label"] == "Adult", "kin, profession, age");
    check(me["attributes"].size() == 6 && me["attributes"][0]["key"] == "STR" && me["attributes"][0]["value"] == 15 && me["attributes"][0]["baseChance"] == 6 &&
              me["attributes"][2]["value"] == 12 && me["attributes"][2]["baseChance"] == 5,
          "attributes with their base chance");
    check(me["hp"]["current"] == 10 && me["hp"]["max"] == 13 && me["wp"]["max"] == 10, "HP 10/13 and WP 10/10");
    check(me["derived"]["movement"] == 10, "movement: kin 10, AGL 12 changes nothing");
    check(me["derived"]["damageBonus"]["str"] == "D4" && me["derived"]["damageBonus"]["agl"] == "", "damage bonus D4 for STR 15, none for AGL 12");
    check(me["derived"]["encumbrance"]["carried"] == 3 && me["derived"]["encumbrance"]["limit"] == 8, "carrying 3 of 8 (rations count one per four)");
    check(me["conditions"].size() == 6 && std::ranges::none_of(me["conditions"], [](const json& c) { return c["active"].get<bool>(); }), "six conditions, none active");
    check(me["party"] == "The Misty Vale party", "the party's name");
    check(me["about"]["weakness"] == "Child of the Wild. I never sleep indoors." && me["about"]["notes"] == "Owes the innkeeper 3 silver.", "weakness and notes");
    check(me["equipment"]["coins"]["silver"] == 1, "coins");

    // skills
    const json& skills = me["skills"];
    const json* axes = find(skills, "name", "Axes");
    const json* acro = find(skills, "name", "Acrobatics");
    check(axes && (*axes)["level"] == 12 && (*axes)["trained"] == true && (*axes)["attribute"] == "STR" && (*axes)["category"] == "weapon", "Axes: trained at 12");
    check(acro && (*acro)["level"] == 5 && (*acro)["trained"] == false && (*acro)["base"] == 5, "Acrobatics: untrained, base chance 5 (AGL 12)");
    check(find(skills, "name", "Evade") && (*find(skills, "name", "Evade"))["level"] == 10, "Evade trained at 10");
    check(!find(skills, "name", "Weapon Skills") && !find(skills, "name", "Animism"), "the weapon-skill heading and the schools of magic are left out");
    bool orderOk = true;
    int last = 0;
    for (const json& s : skills) {
        const int rank = s["category"] == "core" ? 0 : s["category"] == "weapon" ? 1 : 2;
        orderOk &= rank >= last;
        last = rank;
    }
    check(orderOk, "core skills first, then weapon skills");
}

void resolution() {
    Env e = makeEnv("resolve");
    WebApp app(e.config);
    const std::string token = tokenOf(app, "Brenna");
    json me = body(get(app, "/api/me", token));
    const json* veteran = find(me["abilities"], "name", "Veteran");
    check(veteran && (*veteran)["found"] == true && (*veteran)["subtitle"] == "Heroic ability" && contains((*veteran)["body"], "hardened fighter") &&
              (*veteran)["source"] == "Rulebook",
          "abilities carry their card from the loaded content");
    const json* star = find(me["equipment"]["weapons"], "name", "Morningstar");
    bool damage = false;
    if (star)
        for (const json& f : (*star)["stats"]) damage |= f["label"] == "Damage" && f["value"] == "D10";
    check(damage, "a weapon shows its damage");
    check(me["equipment"]["armor"]["name"] == "Chainmail" && me["equipment"]["armor"]["stats"].size() == 1 && me["equipment"]["armor"]["stats"][0]["value"] == "4" &&
              me["equipment"]["helmet"].is_null(),
          "armor with its rating, no helmet");

    // a reference to something no pack provides still shows its name
    json j = readJson(charFile(e, app, "Brenna"));
    j["abilities"].push_back({{"key", "gone/ability/vanished"}, {"name", "Vanished Power"}});
    test::write(charFile(e, app, "Brenna"), j.dump());
    me = body(get(app, "/api/me", token));
    const json* gone = find(me["abilities"], "name", "Vanished Power");
    check(gone && (*gone)["found"] == false && !gone->contains("body"), "an ability of a removed pack keeps its name");
}

void privacy() {
    Env e = makeEnv("privacy");
    WebApp app(e.config);
    const std::string token = tokenOf(app, "Brenna");
    const std::string texts[] = {get(app, "/api/me", token).body, get(app, "/api/party", token).body, get(app, "/api/content/spells", token).body,
                                 get(app, "/api/content", token).body};
    bool clean = true;
    std::string leaked;
    for (const std::string& t : texts)
        for (const char* secret : {"GM SECRET", "innkeeper is the cult", "Secret Boss"})
            if (contains(t, secret)) {
                clean = false;
                leaked = secret;
            }
    for (const std::string& t : texts) clean &= !contains(t, token);
    check(clean, "nothing private reaches a player: GM notes, tokens, the bestiary" + (leaked.empty() ? std::string() : " (leaked: " + leaked + ")"));
    check(body(get(app, "/api/me", tokenOf(app, "Garmander")))["name"] == "Garmander", "a token opens exactly one character");
    check(body(get(app, "/api/me?id=" + byName(app, "Brenna")->id, tokenOf(app, "Garmander")))["name"] == "Garmander", "the character is chosen by the token, never by a parameter");
    check(get(app, "/api/characters", token).status == 404, "no route lists characters");
    check(get(app, "/api/content/creatures", token).status == 404, "no bestiary for players");
    check(get(app, "/api/content/tables", token).status == 404, "no tables either");
}

void chat() {
    Env e = makeEnv("chat");
    WebApp app(e.config);
    const std::string brenna = tokenOf(app, "Brenna"), garmander = tokenOf(app, "Garmander");
    const std::string brennaId = byName(app, "Brenna")->id, garmanderId = byName(app, "Garmander")->id;
    check(get(app, "/api/chat").status == 401 && get(app, "/api/chat", "", "10.0.0.1", "POST", "{}").status == 401 && get(app, "/api/chat/m-1/image").status == 401, "the chat needs a token too");
    check(body(get(app, "/api/chat", brenna))["messages"].empty(), "no messages yet: an empty conversation");

    const std::string picture = e.root + "goblin.png";
    test::write(picture, kPng);
    MessageStore gm;                                    // the GM's app
    gm.setPrefDir(e.prefs + "/");
    std::string err;
    const std::vector<std::string> onlyBrenna = {brennaId}, bothOfThem = {brennaId, garmanderId};
    check(gm.sendFromGm(onlyBrenna, false, "", "You recognise the symbol.", picture, &err), "the GM writes Brenna a private message with a picture");
    check(gm.sendFromGm(bothOfThem, true, "The Misty Vale party", "The bridge is out.", "", &err), "and broadcasts a note to the party");

    const json mine = body(get(app, "/api/chat", brenna))["messages"];
    check(mine.size() == 2 && mine[0]["text"] == "You recognise the symbol." && mine[0]["from"] == "gm" && mine[0]["kind"] == "message" && mine[1]["kind"] == "broadcast" && mine[1]["to"] == "The Misty Vale party",
          "Brenna sees both, oldest first, the broadcast marked as one");
    check(mine[0]["image"] == "/chat/" + mine[0]["id"].get<std::string>() + "/image" && mine[1]["image"].is_null(), "a picture is named by the address that serves it");
    const json theirs = body(get(app, "/api/chat", garmander))["messages"];
    check(theirs.size() == 1 && theirs[0]["kind"] == "broadcast" && !contains(get(app, "/api/chat", garmander).body, "symbol"), "the other player gets only the broadcast");

    const std::string imageUrl = "/api" + mine[0]["image"].get<std::string>();
    const WebResponse img = get(app, imageUrl, brenna);
    check(img.status == 200 && img.contentType == "image/png" && img.body == kPng && contains(img.headers.at("Cache-Control"), "private"), "the picture is served with its type");
    check(get(app, imageUrl, garmander).status == 404 && get(app, imageUrl).status == 401 && get(app, "/api/chat/m-nothing/image", brenna).status == 404, "another player cannot fetch it, even knowing the address");
    check(get(app, "/api/chat/../../settings.json/image", brenna).status == 404 && get(app, "/api/chat//image", brenna).status == 404, "a picture is found through the conversation only: no path tricks");

    // the player writes
    WebResponse sent = get(app, "/api/chat", brenna, "10.0.0.1", "POST", R"({"text": "The door is trapped, I think. <b>bold</b>"})");
    check(sent.status == 201 && body(sent)["message"]["from"] == "player" && body(sent)["message"]["text"] == "The door is trapped, I think. <b>bold</b>", "a player writes to the GM");
    MessageStore seenByGm;                              // the GM's app looks at the folder
    seenByGm.setPrefDir(e.prefs + "/");
    check(seenByGm.thread(brennaId).messages.size() == 3 && seenByGm.thread(brennaId).messages.back().from == "player" && seenByGm.unread(brennaId, true) == 1, "and the GM's side reads it, as unread");
    check(body(get(app, "/api/chat", garmander))["messages"].size() == 1, "nobody else sees it: players never write to each other");
    sent = get(app, "/api/chat", brenna, "10.0.0.1", "POST", "{\"image\": \"" + base64(kPng) + "\"}");
    const std::string sentImage = body(sent)["message"]["image"].is_null() ? "" : body(sent)["message"]["image"].get<std::string>();
    check(sent.status == 201 && !sentImage.empty() && get(app, "/api" + sentImage, brenna).body == kPng && get(app, "/api" + sentImage, garmander).status == 404, "a picture from the player is stored and served only to them (and the GM's app)");
    check(get(app, "/api/chat", brenna, "10.0.0.1", "POST", R"({"image": "%%%not base64%%%"})").status == 400 && get(app, "/api/chat", brenna, "10.0.0.1", "POST", "{\"image\": \"" + base64("<svg onload=alert(1)>") + "\"}").status == 400,
          "bytes that are not a picture are refused, whatever they claim");
    check(get(app, "/api/chat", brenna, "10.0.0.1", "POST", "{}").status == 400 && get(app, "/api/chat", brenna, "10.0.0.1", "POST", "not json").status == 400 &&
              get(app, "/api/chat", brenna, "10.0.0.1", "POST", "{\"text\": \"" + std::string(5000, 'a') + "\"}").status == 400,
          "empty, malformed or too long: 400");
    check(get(app, "/api/chat", brenna, "10.0.0.1", "POST", std::string(13 * 1024 * 1024, 'a')).status == 413 && get(app, "/api/me", brenna, "10.0.0.1", "PATCH", std::string(70 * 1024, ' ')).status == 413,
          "a body over the limit is refused, and only the chat may carry a picture-sized one");

    // read markers
    const json thread = body(get(app, "/api/chat", brenna));
    const std::string last = thread["messages"].back()["id"];
    check(get(app, "/api/chat/read", brenna, "10.0.0.1", "POST", "{\"upTo\": \"" + last + "\"}").status == 200 && body(get(app, "/api/chat", brenna))["playerRead"] == last &&
              seenByGm.unread(brennaId, false) == 0,
          "the player marks what they have read");
    check(get(app, "/api/chat/read", brenna, "10.0.0.1", "POST", "{\"upTo\": \"../x\"}").status == 200 && body(get(app, "/api/chat", brenna))["playerRead"] == last, "a marker only moves forward, and a bad id is ignored");
    seenByGm.markRead(brennaId, true, last);
    check(body(get(app, "/api/chat", brenna))["gmRead"] == last, "the player can tell the GM has read it");

    check(get(app, "/api/chat", brenna, "10.0.0.1", "PUT", "{}").status == 405 && get(app, "/api/chat", brenna, "10.0.0.1", "DELETE").status == 405 && get(app, "/api/me", brenna, "10.0.0.1", "POST", "{}").status == 405 &&
              get(app, "/api/chat", brenna, "10.0.0.1", "PATCH", "{}").status == 405 && get(app, "/api/party", brenna, "10.0.0.1", "POST", "{}").status == 405,
          "only the two writes exist: POST /chat and PATCH /me");
    check(contains(get(app, "/api/chat", brenna).headers.at("Content-Security-Policy"), "blob:"), "the page may show a picture it fetched");

    int refused = 0;
    for (int i = 0; i < 80; ++i) refused += get(app, "/api/chat", garmander, "10.0.0.2", "POST", "{\"text\": \"spam\"}").status == 429;
    check(refused >= 15 && get(app, "/api/chat", brenna, "10.0.0.1", "POST", "{\"text\": \"still fine\"}").status == 201, "someone hammering the server is slowed down, and only they are");
}

void editing() {
    Env e = makeEnv("editing");
    WebApp app(e.config);
    const std::string brenna = tokenOf(app, "Brenna"), garmander = tokenOf(app, "Garmander");
    const std::string brennaId = byName(app, "Brenna")->id;
    const std::string file = e.prefs + "/characters/" + brennaId + ".json";
    auto patch = [&](const std::string& token, const std::string& set) { return get(app, "/api/me", token, "10.0.0.1", "PATCH", "{\"set\": " + set + "}"); };

    const json me = body(get(app, "/api/me", brenna));
    check(me["doc"]["hp"] == 10 && me["doc"]["attributes"]["STR"] == 15 && me["doc"]["skills"].is_array() && me["issues"].empty() && me["locked"] == false && me["revision"].is_number(),
          "the sheet comes with the raw fields the editor works on, its open issues, and whether it is locked");
    check(!me["doc"].contains("kin") && !me["doc"].contains("id") && !me["doc"].contains("reviews") && !me["doc"].contains("created_at"), "and not the parts a player cannot change");

    // a plain edit
    WebResponse r = patch(brenna, R"({"hp": 7, "attributes": {"STR": 16}, "nickname": "Grimmer"})");
    json after = body(r);
    check(r.status == 200 && after["hp"]["current"] == 7 && after["attributes"][0]["value"] == 16 && after["nickname"] == "Grimmer" && after["revision"].get<int>() == me["revision"].get<int>() + 1 && after["derived"]["damageBonus"]["str"] == "D4",
          "an edit is applied and the answer is the new sheet, derived numbers recomputed");
    check(body(get(app, "/api/me", brenna))["hp"]["current"] == 7 && readJson(file)["hp"] == 7 && readJson(file)["attributes"]["CON"] == 13 && readJson(file)["player"] == "Sebastián", "it is in the file, and the rest of the file is untouched");
    ChangeLog log;
    log.setPrefDir(e.prefs + "/");
    const auto entries = log.entries(brennaId);
    check(entries.size() == 1 && entries[0].by == "player" && contains(entries[0].summary, "HP 10 → 7") && contains(entries[0].summary, "STR 15 → 16") && entries[0].before["hp"] == 10, "the GM's log records who changed what, and what it was");
    check(body(get(app, "/api/me", garmander))["nickname"] != "Grimmer", "another player's sheet is untouched: the token picks the character, nothing else does");

    // everything a player may change
    r = patch(brenna, R"({"skills": [{"name": "Axes", "key": "core/skill/axes", "attribute": "STR", "level": 13, "trained": true, "marked": true}],
                          "inventory": [{"name": "Torch"}, {"name": "Rope", "count": 2, "note": "50 ft"}], "tiny_items": ["a bone whistle"], "coins": {"gold": 4},
                          "conditions": ["Scared"], "weapons": [{"name": "Axe"}], "armor": null, "helmet": {"name": "Cap"}, "hp_bonus": 2,
                          "abilities": [{"name": "Veteran"}, {"name": "Robust"}], "spells": [], "age": "old", "weakness": "Fear of heights", "notes": "<script>alert(1)</script>"})");
    after = body(r);
    check(r.status == 200 && after["skills"].size() > 0 && after["equipment"]["inventory"].size() == 2 && after["equipment"]["coins"]["gold"] == 4 && after["conditions"][3]["active"] == true &&
              after["equipment"]["helmet"]["name"] == "Cap" && after["equipment"]["armor"].is_null() && after["hp"]["max"] == 15 && after["age"]["id"] == "old" && after["about"]["notes"] == "<script>alert(1)</script>",
          "attributes, skills, items, coins, conditions, gear, maximum HP, age and text: all of it can be changed");
    const json* axes = find(after["skills"], "name", "Axes");
    check(axes && (*axes)["level"] == 13 && (*axes)["marked"] == true, "including an advancement mark");

    // refusing what is not the player's
    for (const char* bad : {R"({"kin": {"name": "Elf"}})", R"({"profession": {"name": "Mage"}})", R"({"school": "Animism"})", R"({"id": "c-x"})", R"({"revision": 1})", R"({"locked": false})", R"({"reviews": {}})", R"({"player": "x"})"}) {
        check(patch(brenna, bad).status == 400, std::string("a player cannot change: ") + bad);
    }
    check(patch(brenna, R"({"hp": "ten"})").status == 400 && patch(brenna, R"({"attributes": {"STR": "big"}})").status == 400 && patch(brenna, R"({"inventory": "everything"})").status == 400, "wrong types are 400, and say why");
    check(contains(patch(brenna, R"({"hp": "ten"})").body, "whole number"), "with a message the player can read");
    check(get(app, "/api/me", brenna, "10.0.0.1", "PATCH", "not json").status == 400 && get(app, "/api/me", brenna, "10.0.0.1", "PATCH", "{}").status == 400 && get(app, "/api/me", "", "10.0.0.1", "PATCH", "{}").status == 401,
          "a body that is not a set is 400; no token is 401");

    // breaking the rules is allowed, and flagged
    std::string many = "[";
    for (int i = 0; i < 14; ++i) many += std::string(i ? "," : "") + "{\"name\": \"Stone " + std::to_string(i) + "\"}";
    many += "]";
    r = patch(brenna, "{\"inventory\": " + many + ", \"attributes\": {\"INT\": 19}, \"hp\": 40}");
    after = body(r);
    std::set<std::string> flagged;
    for (const json& i : after["issues"]) flagged.insert(i["key"]);
    check(r.status == 200 && flagged.count("encumbrance") && flagged.count("attr:INT") && flagged.count("hp") && after["issues"][0]["status"] == "pending" && after["equipment"]["inventory"].size() == 14,
          "carrying too much, an attribute above 18, HP above the maximum: all allowed, all flagged as pending");

    // the GM rules on it (the app does this through the same file)
    Character c = byName(app, "Brenna") ? *byName(app, "Brenna") : Character();
    Character::fromJson(readText(file), c, nullptr);
    c.id = brennaId;
    sheet::review(c, "encumbrance", true);
    sheet::review(c, "attr:INT", false);
    json ruling = json::object();
    json reviews = json::object();
    for (const auto& [k, v] : c.reviews) reviews[k] = {{"status", v.status}, {"value", v.value}, {"at", v.at}};
    ruling["reviews"] = reviews;
    check(sheet::editFile(file, brennaId, ruling, false, &log).ok, "the GM approves the load and rejects the attribute");
    after = body(get(app, "/api/me", brenna));
    std::map<std::string, std::string> status;
    for (const json& i : after["issues"]) status[i["key"]] = i["status"];
    check(!status.count("encumbrance") && status["attr:INT"] == "rejected" && status["hp"] == "pending", "approved: it is normal now; rejected: it stays, marked so; the rest still waits");
    r = patch(brenna, R"({"attributes": {"INT": 14}, "hp": 10})");
    after = body(r);
    check(after["issues"].empty(), "the player fixes the rest by hand, and the flags go away (the approved load stays approved while it stays the same)");
    check(readJson(file).contains("reviews") && readJson(file)["reviews"].contains("encumbrance"), "the ruling is kept in the file while what it judged is unchanged");
    r = patch(brenna, R"({"inventory": [{"name": "Torch"}]})");
    check(body(r)["issues"].empty() && !readJson(file).contains("reviews"), "and forgotten once the problem is fixed");
    r = patch(brenna, "{\"inventory\": " + many + "}");
    check(body(r)["issues"].size() == 1 && body(r)["issues"][0]["key"] == "encumbrance" && body(r)["issues"][0]["status"] == "pending", "so the same problem later is judged again, not waved through"); 

    // the lock
    Character lockedSheet;
    Character::fromJson(readText(file), lockedSheet, nullptr);
    lockedSheet.locked = true;
    test::write(file, lockedSheet.toJson());
    r = patch(brenna, R"({"hp": 1})");
    check(r.status == 423 && contains(r.body, "locked") && body(get(app, "/api/me", brenna))["locked"] == true, "a locked sheet refuses the player and says so");
    check(sheet::editFile(file, brennaId, body(get(app, "/api/health")).is_null() ? json() : json{{"locked", false}}, false, nullptr).ok && patch(brenna, R"({"hp": 2})").status == 200, "until the GM unlocks it");

    // speed: a player dragging a number sends many edits
    int failures = 0;
    for (int i = 0; i < 40; ++i) failures += patch(garmander, "{\"hp\": " + std::to_string(i % 9 + 1) + "}").status != 200;
    check(failures == 0, "forty quick edits in a row are fine");
}

void party() {
    Env e = makeEnv("party");
    WebApp app(e.config);
    const json p = body(get(app, "/api/party", tokenOf(app, "Brenna")))["party"];
    check(p["name"] == "The Misty Vale party" && p["members"].size() == 3, "the party and its three members");
    int you = 0;
    std::string who;
    for (const json& m : p["members"])
        if (m["you"] == true) {
            ++you;
            who = m["name"];
        }
    check(you == 1 && who == "Brenna", "exactly one member is 'you'");
    std::vector<std::string> keys;
    for (auto it = p.begin(); it != p.end(); ++it) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());
    std::vector<std::string> memberKeys;
    for (auto it = p["members"][0].begin(); it != p["members"][0].end(); ++it) memberKeys.push_back(it.key());
    std::sort(memberKeys.begin(), memberKeys.end());
    check(keys == std::vector<std::string>{"members", "name"} &&
              memberKeys == std::vector<std::string>{"age", "conditions", "hp", "kin", "name", "nickname", "player", "profession", "wp", "you"},
          "the party view has exactly the public fields");
    const json mate = body(get(app, "/api/party", tokenOf(app, "Garmander")))["party"];
    check(find(mate["members"], "name", "Brenna") && (*find(mate["members"], "name", "Brenna"))["you"] == false, "for a teammate, Brenna is not 'you'");
    check(body(get(app, "/api/party", tokenOf(app, "Groddy")))["party"].is_null(), "a character outside any party sees no party");
}

void rules() {
    Env e = makeEnv("rules");
    WebApp app(e.config);
    const std::string token = tokenOf(app, "Brenna");
    const json summary = body(get(app, "/api/content", token));
    std::vector<std::string> types, packs;
    for (const json& t : summary["types"]) types.push_back(t["id"]);
    for (const json& p : summary["packs"]) packs.push_back(p["id"]);
    check(types == std::vector<std::string>{"spells", "abilities", "skills", "kin", "professions", "weapons", "armor", "gear"}, "the rule types on offer: no creatures, no tables");
    check(packs == std::vector<std::string>{"core", "frostmarch"}, "packs: Core and homebrew; the fixture's settings.json switches hidden-pack off");
    std::vector<std::string> names;
    const json allSpells = body(get(app, "/api/content/spells", token));      // (kept in a variable: a range over a temporary would dangle)
    for (const json& s : allSpells["entries"]) names.push_back(s["name"]);
    std::sort(names.begin(), names.end());
    std::string listed;
    for (const std::string& n : names) listed += n + "; ";
    check(names == std::vector<std::string>{"Blizzard Call", "Frost Nip", "Rime Ward"}, "spells of Core and of the enabled homebrew only (got: " + listed + ")");
    const json found = body(get(app, "/api/content/spells?q=blizzard", token))["entries"];
    check(found.size() == 1 && found[0]["name"] == "Blizzard Call" && found[0]["source"] == "Homebrew · Frostmarch" && found[0]["homebrew"] == true &&
              contains(found[0]["body"], "swirling storm"),
          "search finds homebrew, marked with its source");
    check(body(get(app, "/api/content/spells?q=zzzzz", token))["entries"].empty(), "nothing found is an empty list");
    check(body(get(app, "/api/content/abilities?q=blizzard", token))["entries"].empty(), "a search stays inside its type");
    check(get(app, "/api/content/nonsense", token).status == 404, "an unknown type is a 404");
    test::write(e.prefs + "/settings.json", "{\"format\":1,\"disabled_packs\":[\"hidden-pack\",\"frostmarch\"]}");
    check(body(get(app, "/api/content/spells", token))["entries"].empty(), "when the GM switches a pack off, players stop seeing it");
    test::write(e.prefs + "/settings.json", "{\"format\":1}");
    check(body(get(app, "/api/content/spells", token))["entries"].size() == 4, "and when it is switched on again, the hidden pack shows up");
}

void liveUpdates() {
    Env e = makeEnv("live");
    WebApp app(e.config);
    const std::string token = tokenOf(app, "Brenna");
    json j = readJson(charFile(e, app, "Brenna"));
    j["hp"] = 3;
    j["conditions"] = json::array({"Scared", "Dazed"});
    test::write(charFile(e, app, "Brenna"), j.dump());
    const json me = body(get(app, "/api/me", token));
    std::vector<std::string> active;
    for (const json& c : me["conditions"])
        if (c["active"] == true) active.push_back(c["name"]);
    check(me["hp"]["current"] == 3 && active == std::vector<std::string>{"Scared", "Dazed"}, "a change made in the GM app shows up on the next request");
    const json p = body(get(app, "/api/party", token))["party"];
    check((*find(p["members"], "name", "Brenna"))["hp"]["current"] == 3, "and the party sees it too");

    // the GM app does not write atomically: a request can land in the middle of a save
    const std::string file = charFile(e, app, "Brenna");
    const std::string good = readText(file);
    test::write(file, good.substr(0, 200));
    const WebResponse during = get(app, "/api/me", token);
    check(during.status == 200 && body(during)["name"] == "Brenna", "a half-written file: the last good version is still served");
    json finished;
    jsonParse(good, finished, nullptr);
    finished["hp"] = 7;
    test::write(file, finished.dump(2));
    check(body(get(app, "/api/me", token))["hp"]["current"] == 7, "and the finished save is picked up");
    check(app.access().tokens().at(byName(app, "Brenna")->id) == token, "the player kept their token through it all");
}

void tokens() {
    Env e = makeEnv("tokens");
    WebApp app(e.config);
    const json saved = readJson(e.prefs + "/web-access.json");
    check(jsonStr(saved, "base_url") == "http://players.test" && saved["tokens"].size() == 4, "web-access.json records the address and a token per character");
    bool shape = true;
    std::set<std::string> distinct;
    for (auto it = saved["tokens"].begin(); it != saved["tokens"].end(); ++it) {
        const std::string t = it.value();
        shape &= t.size() == 32 && t.find_first_not_of("0123456789abcdef") == std::string::npos;
        distinct.insert(t);
    }
    check(shape && distinct.size() == 4, "128 random bits each, all different");
    const std::string brennaId = byName(app, "Brenna")->id;
    check(app.access().linkFor(brennaId) == "http://players.test/?t=" + std::string(saved["tokens"][brennaId]), "the personal link");

    check(webLinkFor(e.prefs, brennaId) == app.access().linkFor(brennaId) && webLinkFor(e.prefs, "c-nobody").empty() && webLinkFor(e.root + "/nowhere", brennaId).empty(),
          "the GM app reads the same link from web-access.json");

    WebApp restarted(e.config);
    check(restarted.access().tokens() == app.access().tokens(), "a restart keeps every link working");

    const std::string old = tokenOf(app, "Brenna");
    const std::string fresh = app.access().regenerate(brennaId);
    check(fresh != old && get(app, "/api/me", old).status == 401 && get(app, "/api/me", fresh).status == 200, "regenerating: the old link stops working at once");

    const std::string groddy = tokenOf(app, "Groddy");
    const std::string groddyFile = charFile(e, app, "Groddy");
    SDL_RemovePath(groddyFile.c_str());
    check(get(app, "/api/me", groddy).status == 401 && byName(app, "Groddy") == nullptr, "a deleted character has no access any more");

    // a character created while the server runs can sign in right away
    json c = readJson(charFile(e, app, "Brenna"));
    c["id"] = "c-newcomer";
    c["name"] = "Newcomer";
    test::write(e.prefs + "/characters/c-newcomer.json", c.dump());
    get(app, "/api/me", fresh);                          // any request notices the new file
    const std::string token = app.access().tokens().count("c-newcomer") ? app.access().tokens().at("c-newcomer") : std::string();
    check(token.size() == 32 && body(get(app, "/api/me", token))["name"] == "Newcomer", "a new character gets a link and can use it immediately");
}

void guessing() {
    Env e = makeEnv("guess");
    e.config.maxFailures = 5;
    WebApp app(e.config);
    for (int i = 0; i < 5; ++i) get(app, "/api/me", "wrong-token-number-" + std::to_string(i) + "-xxxxxxxx", "6.6.6.6");
    check(get(app, "/api/me", tokenOf(app, "Brenna"), "6.6.6.6").status == 429, "after repeated failures an address is blocked, even with a valid token");
    check(get(app, "/api/me", tokenOf(app, "Brenna"), "7.7.7.7").status == 200, "other addresses are not affected");
    long long clock = 0;
    Env e2 = makeEnv("guess2");
    e2.config.maxFailures = 2;
    e2.config.failureWindowMs = 1000;
    WebApp timed(e2.config, [&clock] { return clock; });
    get(timed, "/api/me", "bad-token-1-xxxxxxxxxxxx", "8.8.8.8");
    get(timed, "/api/me", "bad-token-2-xxxxxxxxxxxx", "8.8.8.8");
    check(get(timed, "/api/me", tokenOf(timed, "Brenna"), "8.8.8.8").status == 429, "blocked inside the window");
    clock = 1500;
    check(get(timed, "/api/me", tokenOf(timed, "Brenna"), "8.8.8.8").status == 200, "and allowed again once the window has passed");
}

void staticClient() {
    Env e = makeEnv("static");
    const std::string dist = e.root + "/client-dist";
    test::write(dist + "/index.html", "<!doctype html><title>players</title>");
    test::write(dist + "/assets/app.js", "console.log(1)");
    test::write(e.root + "/secret.txt", "not for browsers");
    e.config.staticDir = dist;
    WebApp app(e.config);
    const WebResponse root = get(app, "/");
    check(root.status == 200 && contains(root.body, "<title>players</title>") && root.contentType.find("text/html") == 0, "the client's index is served");
    check(root.headers.at("X-Content-Type-Options") == "nosniff" && root.headers.at("Referrer-Policy") == "no-referrer" && root.headers.at("X-Frame-Options") == "DENY" &&
              contains(root.headers.at("Content-Security-Policy"), "default-src 'self'"),
          "security headers");
    check(contains(get(app, "/party").body, "players"), "a deep link falls back to the app");
    const WebResponse js = get(app, "/assets/app.js");
    check(js.status == 200 && js.contentType.contains("javascript") && js.body == "console.log(1)" && contains(js.headers.at("Cache-Control"), "max-age"),
          "assets are served with their type and cached");
    check(!contains(get(app, "/../secret.txt").body, "not for browsers") && !contains(get(app, "/assets/../../secret.txt").body, "not for browsers"), "a path cannot climb out of the client's folder");
    check(get(app, "/", "", "1.1.1.1", "POST").status == 405, "the client is read-only too");
    check(get(app, "/api/nope").status == 401, "unknown API paths are not the app either");
    check(get(app, "/api/me").headers.at("Cache-Control") == "no-store", "API answers are never cached");
    e.config.staticDir = e.root + "/missing";
    WebApp none(e.config);
    check(get(none, "/").status == 200 && contains(get(none, "/").body, "API is running") && get(none, "/party").status == 404, "without a built client the API still runs");
}

void overHttp() {
    Env e = makeEnv("http");
    const std::string dist = e.root + "/client-dist";
    test::write(dist + "/index.html", "<!doctype html><title>players</title>");
    e.config.staticDir = dist;
    WebApp app(e.config);
    WebServer server(app);
    const int port = server.start("127.0.0.1", 0);
    check(port > 0, "the server binds a port");
    if (port <= 0) return;
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(5);
    const std::string token = tokenOf(app, "Brenna");
    auto health = cli.Get("/api/health");
    check(health && health->status == 200 && contains(health->body, "true"), "GET /api/health over HTTP");
    auto denied = cli.Get("/api/me");
    check(denied && denied->status == 401, "no token: 401 over HTTP");
    auto me = cli.Get("/api/me", {{"Authorization", "Bearer " + token}});
    check(me && me->status == 200 && me->get_header_value("Content-Type").find("application/json") == 0 && jsonStr(body(WebResponse{200, "", me->body, {}}), "name") == "Brenna" &&
              me->get_header_value("Cache-Control") == "no-store" && me->get_header_value("X-Frame-Options") == "DENY",
          "GET /api/me with a Bearer token over HTTP: JSON, no-store, security headers");
    auto q = cli.Get("/api/content/spells?q=nip", {{"Authorization", "Bearer " + token}});
    check(q && q->status == 200 && contains(q->body, "Frost Nip") && !contains(q->body, "Rime Ward"), "the query string reaches the app");
    auto post = cli.Post("/api/me", "{}", "application/json");
    check(post && (post->status == 401 || post->status == 405), "a POST is not a way in");
    auto page = cli.Get("/party");
    check(page && page->status == 200 && contains(page->body, "players"), "a deep link over HTTP falls back to the client");
    auto climb = cli.Get("/assets/%2e%2e/%2e%2e/secret.txt");
    check(climb && !contains(climb->body, "not for browsers"), "an encoded path cannot climb out either");
    server.stop();
}

// Behind a tunnel every request comes from the tunnel: failed attempts must be counted per real client, or one guesser
// would lock out all the players.
void behindProxy() {
    Env e = makeEnv("proxy");
    e.config.maxFailures = 3;
    e.config.trustProxy = true;
    WebApp app(e.config);
    WebServer server(app);
    const int port = server.start("127.0.0.1", 0);
    check(port > 0, "the server binds a port (proxy mode)");
    if (port <= 0) return;
    httplib::Client cli("127.0.0.1", port);
    const std::string token = tokenOf(app, "Brenna");
    auto as = [&](const std::string& client, const std::string& bearer) { return cli.Get("/api/me", {{"Authorization", "Bearer " + bearer}, {"X-Forwarded-For", client}}); };
    for (int i = 0; i < 3; ++i) as("203.0.113.9", "wrong-token-number-" + std::to_string(i) + "-xxxxxxxx");
    auto guesser = as("203.0.113.9", token);
    auto player = as("198.51.100.7", token);
    check(guesser && guesser->status == 429, "the client that keeps guessing is blocked (by X-Forwarded-For)");
    check(player && player->status == 200, "a different player behind the same tunnel is not");
    auto chain = as("198.51.100.7, 10.0.0.2", token);
    check(chain && chain->status == 200, "a chain of proxies: the first address is the client");
    server.stop();
}

void realData() {
    if (!looksLikePack(std::string(SKALDBOK_DEV_DATA_DIR) + "/packs/core")) {
        std::printf("  (no real data/ here: skipping the check against the books' Core pack)\n");
        return;
    }
    Env e = makeEnv("real", true);
    WebApp app(e.config);
    check(app.ok(), "the app starts on the real data: " + app.error());
    const std::string token = tokenOf(app, "Brenna");
    const json me = body(get(app, "/api/me", token));
    const json* veteran = find(me["abilities"], "name", "Veteran");
    check(veteran && (*veteran)["found"] == true && (*veteran)["body"].get<std::string>().size() > 20 && (*veteran)["source"] == "Rulebook" && (*veteran)["page"].is_string(),
          "the real Core pack resolves the keys the app really writes (Veteran, with its page)");
    check(me["derived"]["movement"] == 10 && find(me["skills"], "name", "Axes") && !me["equipment"]["weapons"].empty() && !me["equipment"]["weapons"][0]["stats"].empty(),
          "real kin movement and equipment stats");
    const json spells = body(get(app, "/api/content/spells", token))["entries"];
    check(spells.size() >= 66, "the real spells are there");
    const std::string everything = get(app, "/api/content/kin", token).body + get(app, "/api/content/gear", token).body + get(app, "/api/content/abilities", token).body;
    check(!contains(everything, "Centaur") && get(app, "/api/content/creatures", token).status == 404, "no creatures leak from the real Bestiary");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);            // if a check crashes, everything before it is already on screen
    authentication();
    sheetView();
    resolution();
    privacy();
    chat();
    editing();
    party();
    rules();
    liveUpdates();
    tokens();
    guessing();
    staticClient();
    overHttp();
    behindProxy();
    realData();
    return test::finish();
}
