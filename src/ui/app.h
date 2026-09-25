// The application shell: window layout, navigation, history, the source filter, the dice bar and the page viewer.
// Everything that shows or edits content is a Module (module.h); the shell only lists the ones that are on.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ui/module.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace gm {

// Optional command-line driven start state (used by the automated screenshots).
struct StartupOptions {
    std::string tab, select, search;
    std::string importPath;           // a pack folder or .zip to install before anything is shown
    bool demoRoll = false;
    bool demoEncounter = false;       // fill the tracker with a sample fight (screenshots only)
    bool demoCharacter = false;       // make a sample character and open it (screenshots only)
    bool demoParty = false;           // make a sample party of three characters (screenshots only)
    bool demoScreen = false;          // fill the Master Screen with a sample board (screenshots only)
    int newCharacter = -1;            // open the creator at this step (0 = the first), pre-filled with a sample from step 1 on
};

class App : public Host {
public:
    App(SDL_Window* window, SDL_Renderer* renderer, Paths paths);
    ~App() override;

    void applyStartup(const StartupOptions& o);
    void frame(float width, float height);    // build the whole UI for one frame
    bool wantsRedraw() const;                 // something is animating or in flight
    // Looks at the files another program (the players' web server) writes: sheets, parties, the chat and the change log, and at the
    // content packs, which are read again when their files are edited (homebrew being written, Core being rebuilt). True if something
    // changed, so the caller draws again. Cheap, and limited to a few times a second.
    bool pollExternal();
    void dropFile(const std::string& path, float x, float y);   // a file dropped on the window: an image is pinned to the Master Screen

    // ---- Host ---------------------------------------------------------------------------------------
    const Paths& paths() const override { return paths_; }
    ContentStore& content() override { return content_; }
    Settings& settings() override { return settings_; }
    TextureCache& textures() override { return textures_; }
    Dice& dice() override { return dice_; }
    CharacterStore& characters() override { return characters_; }
    PartyStore& parties() override { return parties_; }
    MessageStore& messages() override { return messages_; }
    ChangeLog& changes() override { return changes_; }
    PackManager& packManager() override { return *packs_; }
    SDL_Window* window() override { return window_; }
    void goTo(Kind kind, int id) override { navigate(kind, id, true); }
    void showModule(const std::string& id) override;
    void openLink(const std::string& target) override;
    bool kindAvailable(Kind k) const override;
    const std::vector<std::unique_ptr<Module>>& modules() const override { return modules_; }
    const Selection* selection() const override { return hasSel_ ? &sel_ : nullptr; }
    void contentChanged() override { reloadRequested_ = true; }
    void notify(const std::string& text) override;
    void flashRoll() override;
    void rollExpression(const std::string& text, const std::string& label) override;
    bool sourceShown(int sourceId) const override { return !sourcesOff_.count(sourceId); }
    int sourceRevision() const override { return sourceRev_; }

private:
    struct Recent {
        Kind kind;
        std::string key;                      // Kind's stable key (see ContentStore::keyOf)
    };

    // ---- logic (app.cpp)
    void loadContent();
    void reloadContent();
    void navigate(Kind kind, int id, bool pushHistory);
    // The history: every place the user got to (an entry, a rule or one of its sections, a section of an intro, a page), so Back and
    // Forward (the buttons, Alt+Left / Alt+Right, the mouse's side buttons) retrace links as well as picks.
    struct Place {
        std::string module;                   // the page ("spells", "combat-damage", "gear"...)
        bool entry = false;
        Selection sel;                        // an entry
        int rule = 0, ruleSection = -1;       // a rule (and the section of it), 0 for none
        bool introSection = false;
        Kind introKind = Kind::Spell;
        int section = -1, line = -1;          // a section of an intro
        std::string label;
        bool operator==(const Place& o) const {
            return module == o.module && entry == o.entry && (!entry || sel == o.sel) && rule == o.rule && ruleSection == o.ruleSection &&
                   introSection == o.introSection && (!introSection || (introKind == o.introKind && section == o.section));
        }
    };
    void pushPlace(Place p);                  // after getting somewhere: it becomes the newest place, the ones ahead of it are dropped
    void showPlace(const Place& p);           // gets there (no history is written)
    void goToHistory(int pos);
    void drawHistoryButtons();
    void goBack();
    void goForward();
    Module* handlerFor(Kind k, int id = -1) const;
    Module* moduleById(const std::string& id) const;
    void noteRecent(const Selection& s);
    void loadRecents();
    void saveRecents() const;
    std::string nameOf(const Recent& r);

    // ---- drawing (ui.cpp)
    void setupStyle();
    void drawTopBar();
    void drawGearButton();
    void toggleServerPage();
    void drawNav();
    void drawSourceChips();
    void drawDiceBar();
    void drawWelcome();

    // ---- state
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    Paths paths_;
    Settings settings_;
    ContentStore content_;
    std::unique_ptr<PackManager> packs_;
    CharacterStore characters_;
    PartyStore parties_;
    MessageStore messages_;
    ChangeLog changes_;
    std::map<std::string, int> unreadSeen_;   // per character: unread messages from the player the last time we looked
    double nextPoll_ = 0;
    double nextPackPoll_ = 0;
    std::string packSig_;                     // packSignature() of what is loaded now
    std::string pendingSig_;                  // a newer one seen once: it is loaded when it holds still for a look (a save may be in progress)
    bool reloadFromDisk_ = false;             // the pending reload comes from edited files, not from the app itself
    TextureCache textures_;
    Dice dice_;
    bool reloadRequested_ = false;

    std::vector<std::unique_ptr<Module>> modules_;
    Module* active_ = nullptr;
    Module* beforeServer_ = nullptr;          // what was open when the gear was pressed
    SDL_Texture* gearIcon_ = nullptr;         // the settings gear, a PNG with transparency compiled into the app
    std::set<int> sourcesOff_;                // sources the user switched off in the top bar
    int sourceRev_ = 0;

    Selection sel_;
    bool hasSel_ = false;
    std::vector<Place> history_;
    int historyPos_ = -1;
    Module* recorded_ = nullptr;              // the page the history knows about: a page opened from the rail is a new place
    std::vector<Recent> recents_;             // newest first, kept between sessions
    std::string recentFile_;
    std::map<std::string, std::string> nameCache_;
    float zoom_ = 1.0f;

    // dice bar
    char diceExpr_[40] = "1d20";
    std::deque<DiceRoll> rolls_;
    double rollFlashUntil_ = 0;

    bool focusSearch_ = false;
    std::string toast_;
    double toastUntil_ = 0;

    // Tab toggles keyboard focus between the left nav rail and the main content (list/detail/full), so the whole
    // app can be driven without a mouse: set here on a Tab press, consumed (SetWindowFocus) at the top of the
    // target region on the next frame.
    bool wantFocusNav_ = false;
    bool wantFocusContent_ = false;
    Module* navFocus_ = nullptr;              // the rail entry that had the keyboard highlight last frame
    int navArrowFrame_ = -100;                // the last frame a movement key (arrows, Home/End, PageUp/Down) was pressed
    bool keyboardOnRail_ = true;              // the side that had the keyboard last (where it returns if it gets lost)
    bool pageHadKeyboard_ = false;            // last frame: the page (right of the rail) had keyboard focus
    bool keyboardInUse_ = true;               // arrows/Tab since the last mouse click: show where the keys go
    bool searchActive_ = false;               // the top search box is being typed in (reachable by nav meanwhile)
};

}  // namespace gm
