// The module registry: every module of the app, in navigation order.
#pragma once

#include <memory>
#include <vector>

#include "module.h"

namespace gm {

std::unique_ptr<Module> makeSearchModule(Host& host);
std::unique_ptr<Module> makeMasterScreenModule(Host& host);
std::unique_ptr<Module> makeEncounterModule(Host& host);
std::unique_ptr<Module> makeCharactersModule(Host& host);
std::unique_ptr<Module> makePartyModule(Host& host);
std::unique_ptr<Module> makeMessagesModule(Host& host);
std::unique_ptr<Module> makeWebModule(Host& host);
std::unique_ptr<Module> makeCreaturesModule(Host& host);
std::unique_ptr<Module> makeTablesModule(Host& host);
std::unique_ptr<Module> makeRulesModule(Host& host);
std::unique_ptr<Module> makeSettingsModule(Host& host);

// The card mosaics: spells, abilities, skills, kin, professions, equipment.
enum class Compendium { Spells, Abilities, Skills, Kin, Professions, Equipment };
std::unique_ptr<Module> makeCompendiumModule(Host& host, Compendium which);

std::vector<std::unique_ptr<Module>> createModules(Host& host);

}  // namespace gm
