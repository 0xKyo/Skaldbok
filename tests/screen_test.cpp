// The Master Screen's data: boards and items, stacking, fitting the view, and the per-user save file.
#include <algorithm>
#include <set>
#include <string>

#include "game/master_screen.h"
#include "testutil.h"

using namespace gm;
using test::check;

namespace {

ScreenItem item(const char* type, float x, float y, float w = 200, float h = 100) {
    ScreenItem i;
    i.type = type;
    i.x = x;
    i.y = y;
    i.w = w;
    i.h = h;
    return i;
}

}  // namespace

int main() {
    // ----------------------------------------------------------------------------------------- boards
    MasterScreenData d;
    Board& first = d.ensureBoard();
    check(d.boards.size() == 1 && d.activeId == first.id && !first.id.empty() && first.zoom == 1.0f, "there is always one board");
    Board& second = d.addBoard("Session 2");
    const std::string secondId = second.id;
    check(d.boards.size() == 2 && d.activeId == secondId && d.active()->name == "Session 2", "a new board becomes the active one");
    check(d.addBoard("").name == "Board 3", "an unnamed board gets a number");
    check(d.removeBoard(secondId) && d.boards.size() == 2 && d.find(secondId) == nullptr, "a board can be removed");
    check(d.find(d.activeId) != nullptr, "the active board is always a real one");
    d.removeBoard(d.boards[0].id);
    check(d.boards.size() == 1 && !d.removeBoard(d.boards[0].id) && d.boards.size() == 1, "the last board cannot be removed");

    // ------------------------------------------------------------------------------------------ items
    Board& b = d.ensureBoard();
    ScreenItem& a = d.addItem(b, item("note", 0, 0));
    const std::string idA = a.id;
    ScreenItem& c = d.addItem(b, item("creature", 300, 50, 400, 500));
    const std::string idC = c.id;
    ScreenItem& t = d.addItem(b, item("table", -200, 400));
    const std::string idT = t.id;
    std::set<std::string> ids{idA, idC, idT};
    check(ids.size() == 3 && !idA.empty(), "every item gets its own id");
    check(b.find(idA)->z < b.find(idC)->z && b.find(idC)->z < b.find(idT)->z, "a new item goes in front");
    ScreenItem& tiny = d.addItem(b, item("note", 0, 0, 5, 5));
    check(tiny.w >= MasterScreenData::kMinItemW && tiny.h >= MasterScreenData::kMinItemH, "an item is never smaller than the minimum");
    d.removeItem(b, tiny.id);

    d.bringToFront(b, idA);
    check(b.find(idA)->z > b.find(idT)->z && b.find(idC)->z < b.find(idT)->z, "bringing an item to the front puts it above the rest");
    const int zA = b.find(idA)->z;
    d.bringToFront(b, idA);
    check(b.find(idA)->z == zA, "an item already in front keeps its number (no runaway counters while clicking)");
    d.bringToFront(b, "i-nothing");

    ScreenItem gone;
    check(d.removeItem(b, idC, &gone) && gone.type == "creature" && gone.w == 400 && b.items.size() == 2 && !d.removeItem(b, idC), "removing an item hands it back (for undo)");
    d.addItem(b, gone);
    check(b.items.size() == 3 && b.find(gone.id) && b.find(gone.id)->z == b.topZ(), "an undone removal comes back in front, same id");

    // ------------------------------------------------------------------------------------------ view
    float x0, y0, x1, y1;
    check(MasterScreenData::bounds(b, x0, y0, x1, y1) && x0 == -200 && y0 == 0 && x1 == 700 && y1 == 550, "bounds hold every item");
    b.items[1].collapsed = true;
    MasterScreenData::bounds(b, x0, y0, x1, y1);
    check(y1 == 550 || y1 < 550, "a collapsed item counts only as its title bar");
    b.items[1].collapsed = false;
    MasterScreenData::fit(b, 1000, 600, 40);
    bool allInside = true;
    for (const ScreenItem& i : b.items) {
        const float sx0 = b.viewX + i.x * b.zoom, sy0 = b.viewY + i.y * b.zoom, sx1 = b.viewX + (i.x + i.w) * b.zoom, sy1 = b.viewY + (i.y + i.h) * b.zoom;
        allInside &= sx0 >= 0 && sy0 >= 0 && sx1 <= 1000 && sy1 <= 600;
    }
    check(allInside && b.zoom >= MasterScreenData::kMinZoom && b.zoom <= 1.5f, "fit: every item is inside the view, at a sane zoom");
    Board empty;
    empty.viewX = 55;
    empty.zoom = 2;
    MasterScreenData::fit(empty, 1000, 600);
    check(empty.viewX == 0 && empty.viewY == 0 && empty.zoom == 1.0f, "fit on an empty board resets the view");
    Board huge;
    huge.items.push_back(item("note", 0, 0, 100000, 100000));
    MasterScreenData::fit(huge, 800, 600);
    check(huge.zoom == MasterScreenData::kMinZoom, "fit never zooms out past the minimum");
    check(MasterScreenData::clampZoom(0.01f) == MasterScreenData::kMinZoom && MasterScreenData::clampZoom(99) == MasterScreenData::kMaxZoom && MasterScreenData::clampZoom(1.2f) == 1.2f, "zoom limits");

    // ----------------------------------------------------------------------------------------- saving
    MasterScreenData rich;
    Board& rb = rich.ensureBoard();
    rb.name = "Friday: la Cripta";
    rb.viewX = -120.5f;
    rb.viewY = 33;
    rb.zoom = 0.75f;
    rb.locked = true;
    ScreenItem n = item("note", 10, 20, 260, 180);
    n.title = "Pistas";
    n.text = "Línea uno\nLínea \"dos\" con ü y \\ barra";
    n.color = 3;
    n.collapsed = true;
    rich.addItem(rb, n);
    ScreenItem cr = item("creature", 300, 20, 420, 520);
    cr.kind = "monster";
    cr.ref = "core/monster/bestiary-centaur";
    rich.addItem(rb, cr);
    ScreenItem im = item("image", 0, 300);
    im.image = "C:/Users/Player/Pictures/mapa.png";
    rich.addItem(rb, im);
    const Board saved = rb;                                       // (a copy: adding a board moves the others in memory)
    rich.addBoard("Second");
    rich.activeId = saved.id;

    MasterScreenData back;
    std::string err;
    check(MasterScreenData::fromJson(rich.toJson(), back, &err), "the Master Screen JSON parses");
    Board* rb2 = back.find(saved.id);
    check(back.boards.size() == 2 && back.activeId == saved.id && rb2 && rb2->name == "Friday: la Cripta" && rb2->viewX == -120.5f && rb2->viewY == 33 &&
              rb2->zoom == 0.75f && rb2->locked && rb2->items.size() == 3,
          "boards keep their name, view, lock and items");
    const ScreenItem* n2 = rb2 ? rb2->find(saved.items[0].id) : nullptr;
    check(n2 && n2->title == "Pistas" && n2->text == n.text && n2->color == 3 && n2->collapsed && n2->x == 10 && n2->w == 260 && n2->h == 180, "a note survives with its text (accents, quotes, newlines)");
    const ScreenItem* cr2 = rb2 ? rb2->find(saved.items[1].id) : nullptr;
    check(cr2 && cr2->type == "creature" && cr2->kind == "monster" && cr2->ref == "core/monster/bestiary-centaur" && cr2->z == saved.items[1].z, "a pinned creature is saved as a reference, not a copy");
    check(rb2 && rb2->find(saved.items[2].id)->image == "C:/Users/Player/Pictures/mapa.png", "an image keeps its path");

    // damaged or hand-edited files
    MasterScreenData tolerant;
    check(MasterScreenData::fromJson(
              "{\"boards\":[{\"id\":\"b1\",\"name\":\"X\",\"view\":{\"zoom\":99},\"items\":["
              "{\"type\":\"note\",\"id\":\"same\",\"w\":1,\"h\":1,\"color\":99},{\"type\":\"note\",\"id\":\"same\"},{\"id\":\"no-type\"},5,"
              "{\"type\":\"party\",\"ref\":\"p-1\",\"x\":\"oops\"}]},{\"name\":\"Second\"},7],\"active\":\"missing\"}",
              tolerant, nullptr) &&
              tolerant.boards.size() == 2 && tolerant.activeId == "b1",
          "a hand-edited file loads; an unknown active board falls back to the first");
    const Board& tb = tolerant.boards[0];
    std::set<std::string> tids;
    for (const ScreenItem& i : tb.items) tids.insert(i.id);
    check(tb.zoom == MasterScreenData::kMaxZoom && tb.items.size() == 3 && tids.size() == 3, "zoom is clamped, items without a type are dropped, duplicate ids are made unique");
    check(tb.items[0].w >= MasterScreenData::kMinItemW && tb.items[0].h >= MasterScreenData::kMinItemH && tb.items[0].color == 12, "sizes and colours are clamped");
    check(tb.items[2].x == 0 && tb.items[2].type == "party", "a value of the wrong type falls back to its default");
    MasterScreenData none;
    check(MasterScreenData::fromJson("{}", none, nullptr) && none.boards.size() == 1, "an empty file gives one empty board");
    check(!MasterScreenData::fromJson("[1]", none, &err) && !MasterScreenData::fromJson("{ nope", none, &err), "garbage is refused");

    // the file on disk
    const std::string dir = test::scratch("screen");
    const std::string path = dir + "/master_screen.json";
    SDL_RemovePath(path.c_str());
    MasterScreenData fresh;
    check(!fresh.load(path) && fresh.boards.empty(), "no file yet: nothing loaded, nothing invented");
    check(rich.save(path, &err) && !test::exists(path + ".tmp"), "saving leaves no temporary file behind");
    MasterScreenData loaded;
    check(loaded.load(path) && loaded.boards.size() == 2 && loaded.find(saved.id)->items.size() == 3, "the saved file loads back");
    test::write(path, "{ half a file");
    MasterScreenData keep = loaded;
    check(!keep.load(path) && keep.boards.size() == 2, "a damaged file is refused and the data in memory is untouched");
    check(!rich.save("", nullptr), "saving needs a path");
    SDL_RemovePath(path.c_str());

    return test::finish();
}
