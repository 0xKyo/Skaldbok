// The module registry: every module of the app, in navigation order.
#pragma once

#include <memory>
#include <vector>

#include "ui/module.h"

namespace gm {

std::unique_ptr<Module> makeSearchModule(Host& host);
std::unique_ptr<Module> makeMasterScreenModule(Host& host);
std::unique_ptr<Module> makeEncounterModule(Host& host);
std::unique_ptr<Module> makeCharactersModule(Host& host);
std::unique_ptr<Module> makePartyModule(Host& host);
std::unique_ptr<Module> makeMessagesModule(Host& host);
std::unique_ptr<Module> makeWebModule(Host& host);
std::unique_ptr<Module> makeGearModule(Host& host);
std::unique_ptr<Module> makeRulesModule(Host& host);
// A page per top-level chapter of rules.json that asks for one ("nav": "<group of the nav rail>"), in file order.
// Made once at startup: a chapter that gains or loses "nav" in a live reload shows up after a restart.
std::vector<std::unique_ptr<Module>> makeChapterModules(Host& host);

// A catalog page (list + detail, and its intro) for a kind that has a page of its own (kindsWithOwnPage(), model.cpp).
// Weapons, armor and gear have none: they are the book's own tables, in gear.json's own intro (see gear_module.cpp).
std::unique_ptr<Module> makeCatalogModule(Host& host, Kind kind);

std::vector<std::unique_ptr<Module>> createModules(Host& host);

}  // namespace gm
