#include "ui/modules/modules.h"

namespace gm {

// Navigation order. Add a module here (and a source file in CMakeLists.txt) and it appears in the sidebar.
std::vector<std::unique_ptr<Module>> createModules(Host& host) {
    std::vector<std::unique_ptr<Module>> m;
    m.push_back(makeSearchModule(host));
    m.push_back(makeMasterScreenModule(host));
    m.push_back(makeEncounterModule(host));
    m.push_back(makeCharactersModule(host));
    m.push_back(makePartyModule(host));
    m.push_back(makeMessagesModule(host));
    m.push_back(makeWebModule(host));
    for (Kind k : kindsWithOwnPage()) m.push_back(makeCatalogModule(host, k));   // creatures, spells, abilities... (model.cpp)
    m.push_back(makeGearModule(host));
    // chapters of rules.json with a page of their own: the Reference ones before Rules, other groups (Adventures) after it
    std::vector<std::unique_ptr<Module>> chapters = makeChapterModules(host), later;
    for (auto& c : chapters) (std::string(c->group()) == "Reference" ? m : later).push_back(std::move(c));
    m.push_back(makeRulesModule(host));
    for (auto& c : later) m.push_back(std::move(c));
    return m;
}

}  // namespace gm
