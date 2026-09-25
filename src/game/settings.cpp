#include "game/settings.h"

#include <algorithm>

#include "parsing/fsutil.h"
#include "parsing/jsonutil.h"

namespace gm {

void Settings::open(const std::string& file) {
    file_ = file;
    if (file_.empty()) return;
    const auto found = jsonLoad(file_);
    if (!found) return;                              // missing or damaged: a bad file must never stop the app, use the defaults
    const json& j = *found;
    for (const std::string& id : jsonStrings(j, "disabled_packs")) disabledPacks_.insert(id);
    if (const json* w = jsonFind(j, "web")) {
        web.launchOnOpen = jsonBool(*w, "launch_on_open", web.launchOnOpen);   // (the old "autostart" key is ignored: the server is opt-in now)
        web.port = std::clamp(jsonInt(*w, "port", web.port), 1, 65535);
        web.publicUrl = jsonStr(*w, "public_url");
    }
}

bool Settings::save() const {
    if (file_.empty()) return false;
    json j;
    j["format"] = 1;
    j["disabled_packs"] = disabledPacks_;
    j["web"] = {{"launch_on_open", web.launchOnOpen}, {"port", web.port}, {"public_url", web.publicUrl}};
    return fs::writeFile(file_, j.dump(2) + "\n");
}

void Settings::setPackEnabled(const std::string& id, bool on) {
    if (on) disabledPacks_.erase(id);
    else disabledPacks_.insert(id);
    save();
}

}  // namespace gm
