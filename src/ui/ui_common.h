// Drawing pieces shared by several modules: colours, headings, tables, source badges, filterable lists.
#pragma once

#include <cfloat>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

#include "ui/module.h"

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
// "Original: Rulebook p.18", small grey text (not a link: the app never ships or opens the books' own PDFs).
void pageLink(Host& host, int sourceId, const PageRef& ref, const std::string& pageNote);
// Title, source badge and subtitle, with a "Pin" button (top right, if a Master Screen is available). The page
// reference is not part of the header any more - call pageLink() again at the bottom of the page instead.
void detailHeader(Host& host, const std::string& title, const std::string& subtitle, int sourceId, const Selection& shown);
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
bool inputMultiline(const char* id, std::string& s, ImVec2 size, ImGuiInputTextFlags extra = 0);
bool smallInt(const char* id, int& v, int lo, int hi, float width = 64);       // width in 16px-font units (see U)
std::string capitalized(std::string s);

// ---- keyboard: the page (everything right of the nav rail) -------------------------------------------------------
// The shell tells, once per frame before drawing the page, whether the page has the keyboard (Tab from the rail, or a
// click) and whether the keyboard is what the user is driving with (arrows/Tab since the last mouse click).
void setPageKeyboard(bool pageFocused, bool keyboardInUse);
bool pageFocused();
// The page has the keyboard and nothing else is taking keys (a text field being typed in, a widget being held).
bool pageKeys();
// Up / Down / PageUp / PageDown / Home / End scroll the current window while pageKeys().
void keyScroll();
// While the keyboard drives the page, a border in the accent colour around the current window: where the keys go.
void focusFrame();

// ---- pages with an intro -----------------------------------------------------------------------------------------
// The state of one page's intro editor (see intro_page.cpp).
struct IntroEdit {
    struct Section {
        std::string title, body;
    };
    // A table of the page while it is edited; `extra` holds what the editor has no field for (its id, source, page...) as JSON.
    struct TableEdit {
        struct Row {
            std::string roll;
            std::vector<std::string> cells;
        };
        std::string name, dice, extra;
        std::vector<std::string> columns;
        std::vector<Row> rows;
    };
    bool on = false;
    std::string body;
    std::vector<Section> sections;
    std::vector<TableEdit> tables;
    int openTable = -1;                // a table just added: its header opens
    std::string error;
};
// A kind's "intro" (its chapter of the book): body, sections and tables in one scrolling child; the arrows scroll it. The Edit button
// turns the text into fields and Save writes them back to the JSON the intro was read from.
void introPage(Host& host, Kind kind, IntroEdit& edit, const char* id);
// Brings the page of `kind` to its Intro, scrolled to the section with that index (a link to a section).
void openIntroSection(Host& host, Kind kind, int section, int line = -1);

// "Intro" | <page>: two tabs that never take keyboard focus; Left / Right switch them while the page has the keyboard.
struct IntroTabs {
    bool showingIntro = false;         // last frame
    bool wantIntro = false;
    bool wantPage = false;             // set it to bring the page to the front (a jump to one of its entries)
    IntroEdit edit;
};
void introTabs(Host& host, Kind kind, IntroTabs& tabs, const char* id, const char* pageLabel, const std::function<void()>& page);

// ---- game content entries as the sheet and the creator use them
const Entry* entryFor(const ContentStore& cs, Kind kind, const Ref& r);       // by key, else by name
void entryTooltip(const Entry* e);                                             // the card, while the last item is hovered
// A popup with a filter box listing entries of the given kinds; sets `out` and returns true when one is chosen.
bool pickEntry(const ContentStore& cs, const char* popup, std::initializer_list<Kind> kinds, const Entry*& out);

// A read-only piece of text that can be selected and copied (Ctrl+C) like a normal text field, but reads as plain wrapped
// text: no border, no background. `id` must be stable across frames while this exact text is on screen (e.g. a fixed
// string, or one built from a stable index) so a selection made by dragging survives from frame to frame.
void copyableText(const char* id, const std::string& text, float wrapWidth = 0);
// Splits `text` on '\n' into copyable, wrapped lines. A line starting "✦Label: " gets a coloured label; "- " or
// "* " gets a bullet (two leading spaces per level nest it); "3. " gets a number, kept exactly as written (no renumbering). A blank line is a small gap.
// `scrollKey` names this text for scrollToLine(); `firstLine` is the number (in the whole text) of the first line of `text`, when it is a
// piece of a longer one.
void paragraphs(const std::string& text, const char* id = "p", const char* scrollKey = nullptr, int firstLine = 0);
// The next time the text called `key` is drawn, it scrolls (the window it is in) to its line number `line`.
void scrollToLine(const std::string& key, int line);
// Lets the links written in a text ("[[Keyword]]", "[[label|target]]", "[label](https://...)") open what they point to: paragraphs() draws
// a line that has links word by word, each link clickable (Host::openLink); a line without any stays selectable text.
void setLinkHost(Host* host);
// A line that is only "{{table: Name}}" shows that table (any table of the content, by its title) right there, in any text drawn by
// paragraphs(). tableMarker() tells whether a line is one, and gives the name.
bool tableMarker(const std::string& line, std::string& name);
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
    bool focusFilter_ = false;                       // Ctrl+F: the filter takes the keyboard next frame
    bool filterActive_ = false;                      // the filter is being typed in
};

}  // namespace gm::ui
