// Drawing pieces shared by several modules: colours, headings, tables, source badges, filterable lists.
#pragma once

#include <cfloat>
#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

#include "module.h"

namespace gm::ui {

// The sheet's palette: ink, dragon green, faded ink, label brown, logo red, parchment yellow.
extern const ImVec4 kInk, kAccent, kAccentDim, kGrey, kGold, kRed, kRoll;

float lineH();
float U(float v);                                   // pixel sizes written for the default 16px font, scaled with zoom and DPI
void bigText(const char* text, float scale, ImVec4 color);
ImVec4 sourceColor(const ContentStore& content, int sourceId);
ImVec4 tagColor(size_t index);                      // one accent per filter chip / category
std::string lowered(std::string s);

void sourceBadge(Host& host, int sourceId);
// "Original: Rulebook p.18" (opens the page), or for a source without a PDF the source and page as plain text.
void pageLink(Host& host, int sourceId, const PageRef& ref, const std::string& pageNote);
void detailHeader(Host& host, const std::string& title, const std::string& subtitle, int sourceId, const PageRef& ref,
                  const std::string& pageNote);
// A rounded, filled backdrop behind whatever is drawn between begin() and end() (a group's size is not known before it is drawn).
// Not to be used inside a table: both split the window's draw list.
class Backdrop {
public:
    void begin(float width, ImU32 fill, ImU32 edge, float pad, float rounding = 10.0f);
    void end();

private:
    ImDrawList* dl_ = nullptr;
    ImVec2 origin_{};
    float width_ = 0, pad_ = 0, rounding_ = 0;
    ImU32 fill_ = 0, edge_ = 0;
};

// ---- text and number inputs that edit the value in place (strings grow as needed, no fixed buffers)
bool inputStr(const char* id, std::string& s, float width = -FLT_MIN, const char* hint = nullptr);
bool inputMultiline(const char* id, std::string& s, ImVec2 size);
bool smallInt(const char* id, int& v, int lo, int hi, float width = 64);       // width in 16px-font units (see U)
std::string capitalized(std::string s);

// ---- game content entries as the sheet and the creator use them
const Entry* entryFor(const ContentStore& cs, Kind kind, const Ref& r);       // by key, else by name
void entryTooltip(const Entry* e);                                             // the card, while the last item is hovered
// A popup with a filter box listing entries of the given kinds; sets `out` and returns true when one is chosen.
bool pickEntry(const ContentStore& cs, const char* popup, std::initializer_list<Kind> kinds, const Entry*& out);

void paragraphs(const std::string& text);           // "✦Requirement: Gesture" gets a coloured label; the rest is wrapped text
void highlighted(const std::string& snippet);       // a search snippet with \x01 ... \x02 marking the hits
void fieldsTable(const std::vector<Field>& fields, const char* id);
void tableGrid(const DataTable& t, int highlightRow);
// The creature's attack table with a roll button; `rolledRow`/`rolledValue` keep the last roll.
void attacksTable(Host& host, const Monster& m, int& rolledRow, int& rolledValue);

// A filterable list of ListItems (creatures, tables). Arrow keys, PageUp/Down, Home/End move the selection.
struct ListView {
    std::vector<ListItem> all;
    std::vector<int> shown;                          // indices into `all`
    char filter[96] = {};
    bool scrollToSelection = false;

    void set(std::vector<ListItem> items);
    // Draws filter box, count and rows; returns the item the user picked this frame, or nullptr.
    const ListItem* draw(Host& host, const char* hint, const Selection* selected);
    void ensureVisible(Host& host, const Selection& s);   // clears the filter when it hides `s`, and scrolls to it

private:
    void refilter(Host& host);
    const ListItem* step(const Selection* selected, int delta) const;
    std::string appliedFilter_;
    int appliedRevision_ = -1;
};

}  // namespace gm::ui
