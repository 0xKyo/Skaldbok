// The web application, independent of any socket: a request goes in, a response comes out. This makes it testable
// without a network; web_server.h puts it on HTTP.
//
// The players' page: they see their sheet, party and the rules, and can edit their own sheet and write in their own chat, nothing else.
// The GM app writes the files (characters/, parties/, chat/, packs/, settings.json) and so does this, one field at a time; it reads
// them again whenever they change, so what the GM does shows up within a second, whether or not the GM app is running.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/character.h"
#include "parsing/content.h"
#include "game/changelog.h"
#include "game/messages.h"
#include "parsing/packs.h"
#include "game/party.h"
#include "web/web_access.h"
#include "web/web_config.h"

namespace gm {

struct WebRequest {
    std::string method = "GET";
    std::string path;                                   // decoded, without the query
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;         // names in lower case
    std::string ip;                                     // who is asking (failed attempts are counted per address)
    std::string body;                                   // what a POST or PATCH sent
};

struct WebResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> headers;
};

class WebApp {
public:
    using Clock = std::function<long long()>;           // milliseconds; tests pass their own
    explicit WebApp(WebConfig config, Clock clock = {});
    ~WebApp();
    WebApp(const WebApp&) = delete;
    WebApp& operator=(const WebApp&) = delete;

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    const WebConfig& config() const { return config_; }

    WebResponse handle(const WebRequest& request);       // thread-safe

    // For the command line and the tests.
    WebAccess& access() { return access_; }
    std::vector<Character> characters();                 // what is readable now, sorted by name
    const Character* findCharacter(const std::string& id);
    std::pair<std::string, std::string> nameAndLink(const Character& c) const;

private:
    template <class T>
    struct DirCache {
        struct Item {
            size_t hash = 0, size = 0;                   // of the file's text
            T doc;
        };
        std::map<std::string, Item> items;
        std::set<std::string> present;                   // every .json file, readable or not
        void scan(const std::string& dir, const std::function<bool(const std::string&, T&, const std::string&)>& parse);
        const T* find(const std::string& id) const {
            auto it = items.find(id);
            return it == items.end() ? nullptr : &it->second.doc;
        }
    };

    WebResponse route(const WebRequest& request, long long now);
    WebResponse api(const WebRequest& request, long long now);
    WebResponse serveStatic(const WebRequest& request);
    WebResponse chatImage(const std::string& characterId, const std::string& messageId);
    WebResponse postChat(const WebRequest& request, const std::string& characterId);
    WebResponse editSheet(const WebRequest& request, const std::string& characterId, long long now);
    bool tooManyWrites(const std::string& characterId, long long now);
    void refresh(long long now);
    void reloadContentIfChanged();
    bool blocked(const std::string& ip, long long now);
    void failed(const std::string& ip, long long now);
    const Party* partyOf(const std::string& characterId) const;

    WebConfig config_;
    Clock clock_;
    std::string error_;
    std::mutex mutex_;
    ContentStore content_;
    std::unique_ptr<PackManager> packs_;
    std::string contentSignature_;
    DirCache<Character> characters_;
    DirCache<Party> parties_;
    MessageStore chat_;                                  // the conversation with the GM, written by both programs
    ChangeLog changes_;                                  // what players changed on their sheets
    WebAccess access_;
    long long refreshedAt_ = -1;
    std::map<std::string, std::vector<long long>> failures_;
    std::map<std::string, std::vector<long long>> writes_;      // per character: when they last wrote (chat, sheet edits)
};

}  // namespace gm
