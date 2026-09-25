// The Master Screen's data: boards (infinite canvases) of pinned items, and how they are saved. No UI.
//
// An item is either something the GM typed or picked (a note, an image) or a *reference* to content the app already has (a
// creature, a spell, a table, a book section, a character, a party). References are saved as content keys, never as copies, so a
// pinned character shows its current hit points and a pinned creature follows the pack it came from. Everything is per user:
// it is one JSON file in the user's own folder.
#pragma once

#include <string>
#include <vector>

namespace gm {

struct ScreenItem {
    std::string id;
    std::string type;                 // note | image | entry | creature | table | section | character | party
    std::string kind;                 // for content: its kind ("spell", "monster", "table", "section"...)
    std::string ref;                  // the content key ("core/spell/fetch"; "#12" for a book table or section), a character id, a party id
    float x = 0, y = 0;               // top-left corner, in canvas units
    float w = 320, h = 240;
    int z = 0;                        // stacking: the highest is in front
    int color = 0;                    // sticky-note colour (notes); 0 = the default
    std::string title;                // notes and images: the user's title
    std::string text;                 // notes
    std::string image;                // images: path of the file
    bool collapsed = false;           // only the title bar
};

struct Board {
    std::string id, name;
    float viewX = 0, viewY = 0;       // where the canvas origin sits on screen, relative to the canvas area
    float zoom = 1.0f;
    bool locked = false;              // items cannot be moved, resized or closed by accident
    std::vector<ScreenItem> items;

    ScreenItem* find(const std::string& itemId);
    int topZ() const;
};

class MasterScreenData {
public:
    static constexpr float kMinZoom = 0.25f, kMaxZoom = 3.0f;
    static constexpr float kMinItemW = 150, kMinItemH = 60;

    std::vector<Board> boards;
    std::string activeId;

    Board& ensureBoard();                                        // there is always at least one board
    Board* active();
    Board* find(const std::string& id);
    Board& addBoard(const std::string& name);                    // becomes the active one
    bool removeBoard(const std::string& id);                     // the last board cannot be removed

    ScreenItem& addItem(Board& b, ScreenItem item);              // gives it an id and puts it in front
    bool removeItem(Board& b, const std::string& itemId, ScreenItem* removed = nullptr);
    void bringToFront(Board& b, const std::string& itemId);

    // The rectangle that holds every item; false if the board is empty.
    static bool bounds(const Board& b, float& x0, float& y0, float& x1, float& y1);
    // Pan and zoom the view so every item is visible inside an area of viewW x viewH.
    static void fit(Board& b, float viewW, float viewH, float margin = 40.0f);
    // Clamp a zoom factor to the limits.
    static float clampZoom(float z);

    std::string toJson() const;
    static bool fromJson(const std::string& text, MasterScreenData& out, std::string* error);
    bool load(const std::string& path);                          // false if there is no usable file (the data stays empty)
    bool save(const std::string& path, std::string* error = nullptr) const;

    static std::string newId(const char* prefix);
};

}  // namespace gm
