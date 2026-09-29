[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/T2M427FAPU)

# Skaldbok

**Skaldbok** (from *skald*, the Norse bard, and *bok*, book: the storyteller's book) is a free tool for running **Dragonbane**
games. It gathers the Rulebook, the Bestiary and the *Misty Vale* adventure and shows them in a lightweight desktop app
(C++23, Dear ImGui on SDL3) with the palette of the official character sheet (cream paper, parchment, dragon green and red), and in a
**web interface** for the GM and for each player. It is **modular** (every part can be switched on or off) and supports **homebrew
content** imported as a data plugin.

> Personal use: the original PDFs are copyrighted and are not part of the repository (`References/` is in `.gitignore`).
> `data/` holds the content extracted from them; see [License](#license).

## What it has

Each row is a **module** (the side menu lists them):

| Module | |
|---|---|
| **Search** (always on) | Full-text search over the creatures, spells, rules and tables of the books and of every pack; `Ctrl+K`. |
| **Encounter** | Initiative with cards 1–10, HP, conditions, notes and an attack roll for each creature. Saves itself. Takes creatures and **characters** (their HP stay in sync with the sheet). |
| **Characters** | Player sheets and a step-by-step **character creator** that follows the book (see [Characters](#characters)). |
| **Party** | Groups characters that play together (add/remove, everyone's state at a glance, "Add the party to the encounter", *Message the party…*). |
| **Messages** | A real **chat** with each player (they write from their web page and you see the messages at once, with an unread counter) and a **broadcast** to a party or to everyone, which reaches the players with a style of its own. Text and pictures both ways. Players only talk to the GM, never to each other. |
| **Master Screen** | A personal infinite canvas for the GM: pin creatures, spells, tables, characters, the party, notes and pictures, and move, resize and close them; several boards with tabs (see [Master Screen](#master-screen)). |
| **Web server** *(gear icon, top right)* | The players' web server, **off until you start it** with *Start server* (or the *Launch server when opening* checkbox). A *Web server: on/off* button in the top bar shows whether it is running and opens this tab, where you see the address on your network and each character's personal link (copy it / send it in a message). |
| **Creatures** | Creatures from the three books and from homebrew: illustration, stat block, *Monster Attacks* table with a roll button, abilities, random encounter and adventure seed. If there is something to explain about what monsters are and how they work (the Bestiary chapter of the book), it appears as an **Intro** tab next to the list. |
| **Spells · Abilities · Skills · Kin · Professions** | Like Creatures: a filterable list on the left and the chosen entry on the right, with all its data (requirements, range, full text, its tables). If the category has its general explanation (magic and its schools, how to roll the dice, heroic abilities…), it appears as an **Intro** tab. Abilities has **Generate Ability** and **Edit**; Kin has **Generate Kin** (name, movement, description, names and picture, and its innate abilities picked from a list, with the option of creating a new one on the spot) and **Edit** (including the book's own: your version replaces it everywhere, without touching the original). Generate and Edit save into the same homebrew pack of your own, `custom` (saving again updates the card instead of adding another). **Delete** removes one you created; on an edit of a card from the book or another pack, **Revert to original** undoes your edit. |
| **Gear** | Weapons, armor and equipment: they have no list of their own, they are the book's tables (Armor & Helmets, Melee/Ranged Weapons, Trade Goods…) on a single page, with what each column means. |
| **Character Creation · Combat · World** | The book chapters with a page of their own, under Reference: a tree of sections, a text filter (also by table title), **with their tables inside the section they belong to** (dice tables with a *Roll* button that highlights the result, and *Pin* for the Master Screen). **Character Creation** gathers the book's steps for making a character, numbered and with *See also* links to Kin, Professions, Skills… where the options come from. **World** holds the travel rules (journeys, mishaps, hunting, riding animals), non-player characters with the NPC Creator, treasure and the other hazards (darkness, fear, poison, falling…). They come from the Core pack (see [Content and homebrew](#content-and-homebrew)): a chapter with a `nav` key gets its own page. |
| **Rules** | Shows up only if a pack brings **house rules** or tables that do not belong to a chapter with its own page; without them there is no entry. |

Also: **dice** in the bottom bar (D4–D20 and expressions such as `2D8+3`), **recents** on the home screen and a source filter in the
top bar (each book, and a *Homebrew* menu with one entry per source).

The gear icon is *settings* from [Material Icons](https://github.com/google/material-design-icons) (Apache 2.0; license in
`assets/icons/`), a white PNG with transparency (`tools/make_icons.py` generates it and embeds it in the app), tinted by state.

Shortcuts: `Ctrl+K` search · `Alt+←/→` back and forward · `↑/↓` move through a list · `Ctrl +/−/0` text size.

## Web interface

`skaldbok_web` is a separate server (C++, no window, same core as the app) that reads the GM's files and serves one Vue 3 page
(`web/client`) with two interfaces:

* **GM interface** (`/?gm=<token>`): the whole GM side from a browser.
  * **General Settings** (the first screen) shows the GM link and every player's personal link, ready to copy and send.
  * A sidebar with the **characters** and **parties**; each character opens as an editable sheet plus the chat with that player.
  * A **Reference** index shared with the players, plus what is GM-only:
    * Entries with tabs: Skills (*All skills* / *General info*), Gear (*General* / *Weapons* / *Armor*).
    * Rule tables drawn in place, and each category's Intro always open above its list.
    * **Creatures** with their picture next to the stat block, and **NPC / Animal / Monster** filter tags.
    * **World › NPC Creator**: *Create Random NPC* rolls a name, attitude, kin, motivation, profession and trait from the book's
      lists (`data/system/npcs.yaml`), and each result has its own *Re-roll*.
* **Player interface** (`/?t=<token>`): on their phone and with **their personal link**, each player sees their complete sheet,
  their party's status and the rules (the same Reference index, without what is GM-only). It updates by itself every few seconds,
  lets them edit their sheet and chat with the GM, and it shows no creatures, GM notes or other players' data.

The normal way is to **open `Skaldbok.bat`** and, when you want the players to join, press **Start server** under **the gear icon › Web
server** (or the *Web server: off* button in the top bar; with *Launch server when opening* it starts with the app). Players on the same
Wi-Fi open their link (`http://<your-pc-ip>:8080/?t=<token>`, copied from that tab). The page is compiled once with Node
(`cd web/client && npm install && npm run build`; `Build.bat` does it for you). You can also run it by hand, without the app:

```
build\release\skaldbok_web.exe --public-url http://my-address
```

The links are also printed by `skaldbok_web --links`. Details, security, exposing it over HTTPS and the API: [`docs/WEB.md`](docs/WEB.md).

## Players edit their sheet

On the web, each player can **edit everything** on their character (except kin, profession and school, chosen when it is created in your
app): attributes, HP/WP and their maximums, conditions, skills (level, trained, advancement mark), abilities and spells, weapons, armor,
inventory, coins, texts. What changes syncs by itself, field by field, and you see it **live** even while looking at that sheet (the app
rereads the files about 3 times a second and shows a notice: "Aria changed: HP 10 → 7").

* **The rules do not block, they flag.** If the player goes over (more load than they can carry, an attribute outside 3–18, HP above the
  maximum, a maximum raised without the ability that allows it, a skill above 18), it is allowed but shown **in red**. On your sheet you see
  a *RULE CHECK* banner with **Validate** (it becomes normal) or **Reject** (it stays red: the player has to change it by hand). An approval
  covers exactly what was approved: if they add something else, it asks again.
* **Log and undo**: on the sheet, *Changes made by the player* lists what changed and its previous value, with **Undo**.
* **Lock**: *Lock this sheet* stops that player from editing (they still see their sheet and can chat).
* If both change the same field at once, the last one wins; if they change different fields, both changes are kept.

## Content and homebrew

All the content — rules, creatures, spells, abilities, skills, kin, professions, equipment, tables — lives in **packs**: a folder (or a
`.zip`) with **one YAML file per type** (`rules.yaml`, `creatures.yaml`, `spells.yaml`, …), plus `images/` for the art. The
`manifest.yaml` is **optional**: without it the `id` is the folder name, and the pack's name, books and other data go in the header of
its first data file (Core's are in its `manifest.yaml`). The books' content is a single built-in pack, **Core** (`data/packs/core`), which
opens first and is the base of the others; it holds the creatures, spells, kin, professions, gear… A category that grows a lot may get its
own file. What is not an item of a category goes apart, in `data/system/`: `rules.yaml` (the generic rules and their tables),
`world.yaml` (the World chapter: journeys, non-player characters), `npcs.yaml` (the lists of the NPC generator) and the `intro` of each
page (`spells.yaml`, `creatures.yaml`…); the app shows them in the same place as always.

* **House rules**: a house-rules pack is an ordinary pack with a `rules.yaml`, written as a **nested tree** (each rule lists its
  `children`). Its rules can hang from a Core rule (`parent`) or **replace** it (`replaces`): the rule keeps its place in the tree, shows
  the source *Homebrew · <name>* and says *"Changed by <pack>"*, so it is always clear what is from the book and what is from the house.
  Switching the pack off or removing it brings the rule back as it was.
* **Import**: the gear icon › **General Settings › Content packs** › *Import a pack folder…* or *Import a .zip…* (or
  `skaldbok --import <folder or .zip>` from a terminal). An imported pack carries its `manifest.yaml`, because the `id` cannot be deduced from
  a temporary folder. The pack is validated before it is installed (if something is wrong it is rejected with the file and the position of the
  error) and copied to the user's packs folder (`%APPDATA%\skaldbok\gm\packs\` on Windows). Copying the folder there also works: the app picks it up.
* What is imported is **added** to Core and **keeps its source**: *Homebrew · <name>*, with its color, its filter and its label on every card.
* Each pack can be **switched off** without deleting it (the checkbox on its row), **updated** (import again with the same `id`) or **removed** (*Remove*).
* A pack's creatures are included in search, the encounter and the character creator; a homebrew kin, profession, skill or spell can be
  chosen when creating a character.
* **Live**: the app watches the pack files (Core and the imported ones). If you edit a YAML, the `manifest.yaml` or the images of a pack while
  the app is open, it rereads it by itself within a second and says so ("Content reloaded", or the file's error if it was written wrong).
* `pack_check <folder|zip>` validates a pack from a terminal with the same code as the app.

Full format, with every field of every type: [`docs/HOMEBREW.md`](docs/HOMEBREW.md). A ready-to-import example:
[`docs/examples/frostmarch-tales/`](docs/examples/frostmarch-tales).

## Characters

**Characters** stores each character as a YAML file (`docs/CHARACTERS.md`: the format is meant to be read by the players' web app) and
brings a creator that follows chapter 2 of the Rulebook:

1. kin (pick or roll D12) · 2. profession (pick or roll D10; the mage picks a school of magic) · 3. age (young / adult / old, pick or D6) ·
4. attributes (4D6 dropping the lowest, assigned as they are rolled, a final swap of two, or by hand) · 5. trained skills (6 from the
profession, the rest free; level = double the base chance) · 6. heroic ability or, for the mage, 3 spells and 3 cantrips · 7. starting
gear (one of the three sets; the dice are rolled) · 8. name, nickname, weakness, memento and appearance (with the book's tables) · 9. review.

Everything you can pick comes from the loaded content (Core + active packs). Afterwards the sheet is edited freely: attributes, HP/WP,
conditions, skills (level, advancement mark, D20 roll with dragon/demon, end-of-session advancement roll), abilities, spells, gear (weapons
in hand, armor, helmet, backpack, coins), notes, export/import/duplicate. **Random** creates a whole character at once. From the sheet:
*Add to encounter*, *Message…*, *Pin* (to the Master Screen) and export/duplicate.

The sheet is laid out like the **first (green) page of the official character sheet**, designed for PC: name on the parchment, the six
attribute gems with their condition below (click the diamond to toggle it), damage and movement bonuses, skills in two columns (diamond =
advancement mark; click a name = train; click *(ATTRIBUTE)* = roll a D20), abilities and spells, a numbered inventory, coins, memento and tiny
items, armor, helmet, weapons and the HP and WP circles (click a circle to set the value). Numbers are edited where they are printed: click a
gem to type, mouse wheel to go up or down.

## Master Screen

An **infinite canvas** for the GM alone, with whatever you want at hand as a reference:

* **Pin**: the *Pin* button of a creature, a spell/ability/skill/gear entry (on its cards), a table, a character or a party; the bar's
  *Pin…* button (searches everything); *+ Note*, *+ Image…* or drag an image file onto the window.
* **Move and arrange**: drag the title bar to move, the corner to resize, double-click to fold, × to close (*Undo remove* or `Ctrl+Z`
  brings it back), right-click the title for color, duplicate, bring to front / send to back, open it in its section or send the picture
  to the players. The wheel zooms at the pointer; dragging the background (or the middle button) pans. *Fit* frames everything.
* What is pinned is a **reference** (the content key is saved, not a copy): a character shows its current HP (which can be changed right
  there), a creature follows the pack it came from; if the pack is removed, the item says so.
* **Boards** with tabs (right-click the tab: rename or delete), and a lock so nothing moves by accident.
* It saves itself, **per user**, in `master_screen.json` in the user's folder (atomic writes; a damaged file is set aside as `.damaged`
  instead of being lost).

## Chat and broadcast

The **Messages** module is a real chat, on the same web the players already use. Each character has their conversation with the GM (a player
never sees another's nor can write to them): the list on the left, ordered by activity and with unread counts; the conversation on the right,
with pictures and a "seen" mark when the player has read it. When a player writes, the app notifies you ("Aria: …") even if you are in another
module. **Broadcast…** sends the same message to a party or to everyone: in each conversation it appears as a highlighted banner (red border and
"BROADCAST"), different from an ordinary message, and the players' replies come back to you, one by one.

Each message is its own file (`chat/<character>/<id>.json`, pictures in `chat/media/`) and each side saves only how far it has read, so the GM
and the server can write at the same time without stepping on each other. From a creature, *Send this picture to the players…* opens the form with its art.

## How it is built

```
data/packs/core/    (the Core pack, the base: creatures, spells, kin, professions, gear… + images/)
data/system/        (the books' generic rules and tables, the World chapter, the NPC lists, the intro of each page)
                          ▼
   skaldbok  (C++ / SDL3 / Dear ImGui)  ◄── homebrew and house-rules packs (user folder)
   skaldbok_web  (C++ server + Vue 3 page)  ── the same data, for the GM and the players in a browser
```

1. **Data**: `data/packs/core` and `data/system` (the app no longer reads any database). It comes from your copy of the books; `tools/`
   keeps the Python scripts the data was first extracted with (see `tools/README.md`).
2. **App on Windows** (Visual Studio with the C++ workload). The simplest is `Build.bat`, which compiles the Vue client (if Node is installed)
   and then the C++ release (`Build.bat --test` also runs the tests, `--clean` starts from scratch). Or by hand:
   ```
   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Test
   build\release\skaldbok.exe
   ```
3. **App on Linux**:
   ```
   sudo apt install build-essential cmake ninja-build pkg-config libx11-dev libxext-dev libxcursor-dev libxi-dev \
        libxrandr-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev libdrm-dev libgbm-dev \
        libgl1-mesa-dev libegl1-mesa-dev libdbus-1-dev libudev-dev
   cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release
   ctest --test-dir build/release && build/release/skaldbok
   ```
   *(Only tested on Windows; the code uses nothing platform-specific. It needs a C++23 compiler.)*

CMake downloads SDL3, Dear ImGui, SQLite, stb_image, nlohmann/json, yaml-cpp, miniz (zip) and cpp-httplib (web server) as source files and
compiles them statically: the executable is self-contained. `scripts\build.ps1 -Headless` compiles only the core and the tests, without a window.

The app looks for `data/` (with `packs/core`) next to the executable, one to three levels up, in `$SKALDBOK_DATA` or with `--data <folder>`.
The user's files (settings, recents, encounter, characters, imported packs) go to `%APPDATA%\skaldbok\gm\` (`--prefs <folder>` to change it).

## Lightweight

All the content is read once at startup from YAML; images are decoded on demand with a cache of 12, the search index is built once and the app
does not redraw while nothing changes. With `--software` it uses SDL's software renderer.

## Structure

```
                ── core (libgm_core): no UI and no server; every view uses it ──
src/parsing/    reads YAML and turns it into the internal model: content (packs), packs (import), jsonutil / yamlutil, jsondir
                (per-user stores that reread what another program writes), fsutil (files), fts/sql (search index)
src/game/       the game logic over that model: model (data), character, creation (rules), party, encounter, dice,
                messages (chat), sheet_edit (per-field editing and rule review), changelog, master_screen (canvas data),
                settings, web_link
                ── views: each one links gm_core and nothing else; none includes another ──
src/ui/         the GM's app (ImGui): main, app.cpp (the shell: navigation, history, source filter, dice), module.h (the Module / Host
                contract and the optional services between modules), ui_common, fonts, textures, filedialog,
                homebrew_forms (Generate / Edit of Kin and Abilities)
src/ui/modules/ one file per module (search, master_screen, encounter, characters + character_sheet, party, messages, catalog, gear,
                rules, web). catalog: the list + detail page (with its Intro) of every content type that has its own page according
                to the types table in game/model.cpp; creatures adds its detail
src/web/        the web server: a socketless app (web_app), JSON views (web_views), tokens (web_access), HTTP (web_server)
web/client/     the web page for GM and players (Vue 3 + Vite); built with npm and served by skaldbok_web
tests/          windowless ctest: content and packs, characters and parties, encounter, Master Screen, chat, sheet editing (two writers on one
                file) and the web (authentication, privacy, real HTTP); the page's own tests are in web/client (npm test)
tools/          the PDF → SQLite converter + Core pack in Python, and pack_check
docs/           HOMEBREW.md (pack format), CHARACTERS.md (characters and parties), WEB.md (the web interfaces)
docs/examples/  frostmarch-tales: an example pack, documentation only (the app does not use it)
scripts/        build.ps1, package.ps1, deploy.ps1 (Windows)
Build.bat       builds the Vue client and the C++ release in one go
```

**Adding a view** (another API, another UI): a new target that links `gm_core`; everything it needs (reading packs, characters, rules) is
already there. Whatever has to be shared between views goes into the core, never from one view to another.

**Adding a module**: a class deriving from `Module` in `src/ui/modules/`, its factory in `modules.h` and a line in `registry.cpp`. It shows
up in the menu by itself. A module only talks to the others through `Host` and the optional services (`serviceOf<IEncounterSink>(host)`
returns null if no module offers it, and the rest copes with that).

Command-line options for testing: `--shot file.png` (capture and exit), `--tab <module id>`, `--select <name>`, `--search <text>`,
`--roll`, `--page N`, `--demo-encounter`, `--demo-character`, `--demo-party`, `--demo-screen`, `--new-character <step>`, `--import <pack>`,
`--prefs <folder>` (isolates settings, recents, characters and packs), `--size 1200x800`.

## Data reliability

* The text is literal; hyphenated line breaks are resolved using the book's own vocabulary.
* Dice tables are validated against the die (the rolls must cover 1..N) and the converter cross-checks what it extracted against each book's
  index (number of stat blocks, attack tables). `tools/validate.py` summarizes the state.
* The Rulebook's illustrated drop caps (9 letters) have no text in the PDF; they were read from the rendered pages.
* The attributes of weapon skills are not in the running text: they were taken from the printed character sheet (Rulebook p.127).
* "Page N" references inside the texts are **printed** pages, as in the books.

## Pending / known limits

* Some creatures in the adventure have no stat block in the book: the reference is shown ("stats as per page 87 in the Rulebook") with a
  link to the creature it cites.
* The adventure and Bestiary content that is not creatures (maps, rumors, events) is stored as text and tables, not as entities.
* The old messages of the previous version (the `messages/` folder and `messages-sent.json`, from when they only went from the GM to the
  player) are not migrated; they can be deleted.
* The Master Screen stores images by their path on your disk: if you move the file, the item says it cannot find it.
* The GM web interface does not yet cover everything the desktop app does (character creator, encounter tracker, homebrew management…);
  the desktop app stays until it does.

## License

Free software under the **GNU General Public License v3.0 or later** (full text in [`LICENSE`](LICENSE)): you may use, study, modify and
redistribute it, and any modified version you distribute must also be free under the same license.

* Dependencies (downloaded at build time, `cmake/Dependencies.cmake`), all GPL-compatible: Dear ImGui, cpp-httplib, nlohmann/json,
  yaml-cpp, miniz, stb_image and Vue (MIT), SDL3 (zlib), SQLite (public domain) and Material Icons (Apache 2.0, see `assets/icons/`).
* **Dragonbane** is a trademark and work of Free League Publishing. This project is not affiliated with them. The original PDFs
  (`References/`) are **not** part of the repository. `data/` holds the content extracted from the books (rules, creatures, spells, tables and
  art), which belongs to Free League and is here for personal use; anyone who wants to redistribute it needs the rights to do so. Generating the
  data from your own legal copy of the books is described in `tools/README.md`.
* The homebrew content packs (`docs/examples/`) are original and are published under the same license.
