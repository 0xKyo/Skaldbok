#include "settings.h"

#include <algorithm>

#include "fsutil.h"
#include "jsonutil.h"

namespace gm {

void Settings::open(const std::string& file) {
    file_ = file;
    if (file_.empty()) return;
    const auto found = jsonLoad(file_);
    if (!found) return;                              // missing or damaged: a bad file must never stop the app, use the defaults
    const json& j = *found;
    if (const json* m = jsonFind(j, "modules"); m && m->is_object())
        for (auto it = m->begin(); it != m->end(); ++it)
            if (it.value().is_boolean()) modules_[it.key()] = it.value().get<bool>();
    for (const std::string& id : jsonStrings(j, "disabled_packs")) disabledPacks_.insert(id);
    if (const json* w = jsonFind(j, "web")) {
        web.autostart = jsonBool(*w, "autostart", web.autostart);
        web.port = std::clamp(jsonInt(*w, "port", web.port), 1, 65535);
        web.publicUrl = jsonStr(*w, "public_url");
    }
}

bool Settings::save() const {
    if (file_.empty()) return false;
    json j;
    j["format"] = 1;
    json m = json::object();
    for (const auto& [id, on] : modules_) m[id] = on;
    j["modules"] = m;
    j["disabled_packs"] = disabledPacks_;
    j["web"] = {{"autostart", web.autostart}, {"port", web.port}, {"public_url", web.publicUrl}};
    return fs::writeFile(file_, j.dump(2) + "\n");
}

bool Settings::moduleEnabled(const std::string& id) const {
    const auto it = modules_.find(id);
    return it == modules_.end() || it->second;
}

void Settings::setModuleEnabled(const std::string& id, bool on) {
    modules_[id] = on;
    save();
}

void Settings::setPackEnabled(const std::string& id, bool on) {
    if (on) disabledPacks_.erase(id);
    else disabledPacks_.insert(id);
    save();
}

}  // namespace gm
