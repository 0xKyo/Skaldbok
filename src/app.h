// The application shell: window layout, navigation, history, the source filter, the dice bar and the page viewer.
// Everything that shows or edits content is a Module (module.h); the shell only lists the ones that are on.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "module.h"

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
    int page = 0;                     // open the page viewer on this physical page of the selected creature's book
};

class App : public Host {
public:
    App(SDL_Window* window, SDL_Renderer* renderer, Database& db, Paths paths);
    ~App() override;

    void applyStartup(const StartupOptions& o);
    void frame(float width, float height);    // build the whole UI for one frame
    bool wantsRedraw() const;                 // something is animating or in flight
    // Looks at the files another program (the players' web server) writes: sheets, parties, the chat and the change log. True if
    // something changed, so the caller draws again. Cheap, and limited to a few times a second.
    bool pollExternal();
    void dropFile(const std::string& path, float x, float y);   // a file dropped on the window: an image is pinned to the Master Screen

    // ---- Host ---------------------------------------------------------------------------------------
    const Paths& paths() const override { return paths_; }
    Database& db() override { return db_; }
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
    bool kindAvailable(Kind k) const override;
    const std::vector<std::unique_ptr<Module>>& modules() const override { return modules_; }
    const Selection* selection() const override { return hasSel_ ? &sel_ : nullptr; }
    void contentChanged() override { reloadRequested_ = true; }
    void notify(const std::string& text) override;
    void flashRoll() override;
    void rollExpression(const std::string& text, const std::string& label) override;
    bool sourceShown(int sourceId) const override { return !sourcesOff_.count(sourceId); }
    int sourceRevision() const override { return sourceRev_; }
    void openPage(const PageRef& r) override;
    std::string pdfPath(int sourceId) const override;

private:
    struct Recent {
        Kind kind;
        std::string key;                      // Kind's stable key (see ContentStore::keyOf)
    };

    // ---- logic (app.cpp)
    void loadContent();
    void reloadContent();
    void navigate(Kind kind, int id, bool pushHistory);
    void goBack();
    void goForward();
    Module* handlerFor(Kind k) const;
    Module* moduleById(const std::string& id) const;
    bool isOn(const Module& m) const { return moduleIsOn(const_cast<App&>(*this), m); }
    void noteRecent(const Selection& s);
    void loadRecents();
    void saveRecents() const;
    std::string nameOf(const Recent& r);
    std::string pageImagePath(const PageRef& r) const;
    std::string pdfUrl(const PageRef& r) const;
    void showPage(const PageRef& r);

    // ---- drawing (ui.cpp)
    void setupStyle();
    void drawTopBar();
    void drawGearButton();
    void toggleSettings();
    void drawNav();
    void drawSourceChips();
    void drawDiceBar();
    void drawWelcome();
    void drawViewer(float w, float h);

    // ---- state
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    Database& db_;
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
    TextureCache textures_;
    Dice dice_;
    bool reloadRequested_ = false;

    std::vector<std::unique_ptr<Module>> modules_;
    Module* active_ = nullptr;
    Module* beforeSettings_ = nullptr;        // what was open when the gear was pressed
    SDL_Texture* gearIcon_ = nullptr;         // the settings gear, a PNG with transparency compiled into the app
    std::set<int> sourcesOff_;                // sources the user switched off in the top bar
    int sourceRev_ = 0;

    Selection sel_;
    bool hasSel_ = false;
    std::vector<Selection> history_;
    int historyPos_ = -1;
    std::vector<Recent> recents_;             // newest first, kept between sessions
    std::string recentFile_;
    std::map<std::string, std::string> nameCache_;
    float zoom_ = 1.0f;

    // dice bar
    char diceExpr_[40] = "1d20";
    std::deque<DiceRoll> rolls_;
    double rollFlashUntil_ = 0;

    // in-app viewer of the original pages (needs data/pages/, see tools/render_pages.py)
    bool viewerOpen_ = false;
    PageRef viewerPage_;
    float viewerZoom_ = 0.0f;                 // 0 = fit to width
    bool viewerScrollTop_ = false;

    bool focusSearch_ = false;
    std::string toast_;
    double toastUntil_ = 0;
};

}  // namespace gm
