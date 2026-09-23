#include "modules/modules.h"

namespace gm {

// Navigation order. Add a module here (and a source file in CMakeLists.txt) and it appears in the sidebar and in
// Settings > Modules, where it can be switched on and off.
std::vector<std::unique_ptr<Module>> createModules(Host& host) {
    std::vector<std::unique_ptr<Module>> m;
    m.push_back(makeSearchModule(host));
    m.push_back(makeMasterScreenModule(host));
    m.push_back(makeEncounterModule(host));
    m.push_back(makeCharactersModule(host));
    m.push_back(makePartyModule(host));
    m.push_back(makeMessagesModule(host));
    m.push_back(makeWebModule(host));
    m.push_back(makeCreaturesModule(host));
    for (Compendium c : {Compendium::Spells, Compendium::Abilities, Compendium::Skills, Compendium::Kin, Compendium::Professions})
        m.push_back(makeCompendiumModule(host, c));
    m.push_back(makeGearModule(host));
    m.push_back(makeRulesModule(host));
    m.push_back(makeAdventuresModule(host));
    m.push_back(makeSettingsModule(host));
    return m;
}

}  // namespace gm
