// The character sheet as a page of the app: laid out like the first (green) page of the printed sheet, but for the desktop, so every
// number and name is edited right where it is printed. The Characters module owns the character and saves it when draw() says so.
#pragma once

#include <set>
#include <string>

#include "module.h"
#include "sheet_edit.h"

namespace gm {

class CharacterSheet {
public:
    explicit CharacterSheet(Host& host) : host_(host) {}

    // Draws the whole sheet in the current window; true if anything was edited (the caller saves).
    bool draw(Character& c);
    // Another character is on show: forget what was being edited.
    void reset();

private:
    void oversight(Character& c, bool& changed, float w, const std::vector<sheet::OpenIssue>& issues);
    void header(Character& c, bool& changed, float w);
    void attributes(Character& c, bool& changed, float w);
    void abilities(Character& c, bool& changed, float w);
    void coins(Character& c, bool& changed, float w);
    void skills(Character& c, bool& changed, float w);
    void inventory(Character& c, bool& changed, float w);
    void armorAndWeapons(Character& c, bool& changed, float w);
    void points(const char* title, int& current, int max, int& bonus, const char* bonusTip, bool red, bool& changed, float w);
    void gear(const char* label, Item& item, const char* bane, bool& changed, float w);
    bool refList(const char* label, std::vector<Ref>& list, Kind kind, const char* popup, float w);

    Host& host_;
    int editingAttribute_ = -1;
    bool focusAttribute_ = false;
    std::string message_;                 // what the last skill roll or advancement gave
    std::string newItem_;                 // the free inventory line being typed in
    std::set<std::string> flagged_;       // keys of the rule breaks nobody has approved, so the parts they concern can show it
};

}  // namespace gm
