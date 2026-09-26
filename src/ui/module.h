// The contract between the app shell and its modules.
//
// A module is one self-contained part of the app (Encounter, Characters, Messages, the spells page...). It draws
// itself, keeps its own state and talks to the rest only through Host (shared services and navigation) and the small
// service interfaces below. The shell lists the modules that are switched on; a module that is off is neither drawn
// nor found by the others, so nothing else breaks when it is missing.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "game/changelog.h"
#include "game/character.h"
#include "parsing/content.h"
#include "game/dice.h"
#include "game/messages.h"
#include "parsing/packs.h"
#include "game/party.h"
#include "game/settings.h"
#include "ui/textures.h"

struct SDL_Window;

namespace gm {

struct Selection {
    Kind kind = Kind::Monster;
    int id = 0;
    bool operator==(const Selection& o) const { return kind == o.kind && id == o.id; }
};

struct Paths {
    std::string root;        // project root: holds data/ and References/
    std::string dataDir;
    std::string prefDir;     // per-user files, ends with '/': settings, recents, encounter, characters, imported packs
    std::string coreDir() const { return dataDir + "/packs/core"; }
    std::string userPacksDir() const { return prefDir + "packs"; }
    std::string charactersDir() const { return prefDir + "characters"; }
    std::string partiesDir() const { return prefDir + "parties"; }
    std::string masterScreenFile() const { return prefDir + "master_screen.yaml"; }
};

// ---------------------------------------------------------------------------------- optional services
// A module can offer one of these; the others look it up with serviceOf<T>() and simply do without when it is absent.

// The players' web server, for the shell to show whether it is up (a chip in the top bar that opens its page).
class IWebStatus {
public:
    virtual ~IWebStatus() = default;
    virtual bool webRunning() const = 0;
    virtual bool webStarting() const = 0;
};

class IEncounterSink {
public:
    virtual ~IEncounterSink() = default;
    virtual void addCreature(const Monster& m, const StatBlock* block) = 0;
    virtual void addPlayer(const Character& c) = 0;
    virtual void demoFight(bool selectAttackRoll) = 0;   // a sample fight, for automated screenshots only
};

class IMessenger {
public:
    virtual ~IMessenger() = default;
    // Opens the Messages form with a recipient, a picture and a text filled in. The recipient is "" (keep the current one), "all",
    // "char:<character id>" or "party:<party id>" (each member gets it).
    virtual void compose(const std::string& recipient, const std::string& imagePath, const std::string& text) = 0;
};

// The Master Screen: a canvas of pinned references. Any module can offer a "Pin" button when this service is there.
class IMasterScreen {
public:
    virtual ~IMasterScreen() = default;
    virtual void pinEntry(Kind kind, int id) = 0;                // a creature, spell, ability, table, book section...
    virtual void pinCharacter(const std::string& characterId) = 0;
    virtual void pinParty(const std::string& partyId) = 0;
    virtual void pinImage(const std::string& path, float windowX = -1, float windowY = -1) = 0;   // a dropped file lands where it was dropped
    virtual void demoBoard() = 0;                              // a sample board, for automated screenshots
};

class ICharacters {
public:
    virtual ~ICharacters() = default;
    virtual void openCharacter(const std::string& id) = 0;
    // Open the creation wizard. From step 1 on it is pre-filled with a sample character up to that step (for screenshots).
    virtual void startCreator(int step) = 0;
    virtual void demoCharacter() = 0;                    // a sample character, for automated screenshots
};

class ISearch {
public:
    virtual ~ISearch() = default;
    virtual char* queryBuffer() = 0;
    virtual size_t queryCapacity() const = 0;
    virtual void runQuery() = 0;
};

// ------------------------------------------------------------------------------------------- module

enum class Layout {
    Full,          // the module uses the whole area
    ListDetail     // a list on the left and the selected thing on the right
};

class Host;

class Module {
public:
    explicit Module(Host& host) : host_(host) {}
    virtual ~Module() = default;
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    virtual const char* id() const = 0;                  // "encounter": what settings and --tab use
    virtual const char* title() const = 0;               // the navigation label
    virtual const char* group() const { return ""; }     // navigation heading; modules with the same group sit together
    virtual bool showInNav() const { return true; }      // false: no sidebar entry (the web server page, reached with the gear)
    virtual Layout layout() const { return Layout::ListDetail; }
    virtual int badge() const { return -1; }             // the number next to the label, -1 = none

    // This module shows entries of that kind; `id` is given when routing a specific one (goTo, a reload's carried-over selection),
    // -1 for a general "does anything show this kind at all" query (kindAvailable). A module that splits one Kind across several
    // instances (Rules and Adventures both show Kind::Table) must check `id` to claim only the ones that are really its own.
    virtual bool handles(Kind, int id = -1) const {
        (void)id;
        return false;
    }
    virtual bool findByName(const std::string& lowerName, Selection&) const { (void)lowerName; return false; }

    virtual void onContentChanged() {}                   // packs were loaded, imported, removed or switched
    virtual void onSelect(const Selection&) {}           // navigation reached something this module handles
    virtual void drawFull() {}                           // Layout::Full
    virtual void drawList() {}                           // Layout::ListDetail: the left pane
    virtual bool ownsDetail() const { return false; }    // true: drawDetail() replaces the shell's "selected entry" view
    virtual void drawDetail() {}
    virtual void drawSelection(const Selection&) {}      // draw one entry of a kind this module handles
    virtual void update() {}                             // once per frame (network, timers)
    virtual bool wantsRedraw() const { return false; }   // something is animating or in flight

protected:
    Host& host_;
};

// --------------------------------------------------------------------------------------------- host

class Host {
public:
    virtual ~Host() = default;

    virtual const Paths& paths() const = 0;
    virtual ContentStore& content() = 0;
    virtual Settings& settings() = 0;
    virtual TextureCache& textures() = 0;
    virtual Dice& dice() = 0;
    virtual CharacterStore& characters() = 0;
    virtual PartyStore& parties() = 0;
    virtual MessageStore& messages() = 0;
    virtual ChangeLog& changes() = 0;
    virtual PackManager& packManager() = 0;
    virtual SDL_Window* window() = 0;

    virtual void goTo(Kind kind, int id) = 0;            // open an entry, in whichever module shows that kind
    // Follow a link written in a text: a web address, a "see" reference ("core/spell/birdsong", "spells") or a keyword ("Birdsong").
    virtual void openLink(const std::string& target) = 0;
    virtual void showModule(const std::string& id) = 0;
    virtual bool kindAvailable(Kind k) const = 0;        // is a module that shows this kind switched on
    virtual const std::vector<std::unique_ptr<Module>>& modules() const = 0;
    virtual const Selection* selection() const = 0;      // what the shell currently shows, null if nothing
    virtual void contentChanged() = 0;                   // after packs changed: reload everything and tell the modules

    virtual void notify(const std::string& text) = 0;    // a short message at the bottom
    virtual void flashRoll() = 0;                        // highlight the dice bar for a moment
    virtual void rollExpression(const std::string& text, const std::string& label) = 0;

    virtual bool sourceShown(int sourceId) const = 0;
    virtual int sourceRevision() const = 0;              // changes whenever the source filter does
};

// The module offering service T, or nullptr.
template <class T>
T* serviceOf(Host& host) {
    for (const auto& m : host.modules())
        if (T* s = dynamic_cast<T*>(m.get())) return s;
    return nullptr;
}

}  // namespace gm
