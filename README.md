[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/T2M427FAPU)

# Skaldbok

**Skaldbok** (from *skald*, the Norse bard, and *bok*, book: the storyteller's book) is a free tool for running **Dragonbane**
games. It gathers the Rulebook, the Bestiary and the *Misty Vale* adventure and shows them in a **web interface** for the GM
and for each player. A small desktop app (C++23, Dear ImGui on SDL3) manages the **content packs** and starts the web server. It
supports **homebrew content** imported as a data plugin.

> Personal use: the original PDFs are copyrighted and are not part of the repository (`References/` is in `.gitignore`).
> `data/` holds the content extracted from them; see [License](#license).

## What it has

* **The desktop app** has a single screen, **General Settings**: the content packs (import a folder or .zip, switch a pack on or off, remove
  one, what each brought) and the players' web server (*Start server*, *Launch server when opening*, the port and the address players use,
  *Open in this browser* and *Open as GM*). The window uses the palette of the official
  character sheet (cream paper, parchment, dragon green and red).
* **The web interface** does everything else, for the GM and for the players (see below): characters and their sheets, the character
  creator, parties, chat and broadcasts, and the whole Reference (rules, creatures, spells, abilities, skills, kin, professions, gear and the
  book chapters with their tables).

## Web interface

`skaldbok_web` is a separate server (C++, no window, same core as the app) that reads the GM's files and serves one Vue 3 page
(`web/client`) with two interfaces:

* **GM interface** (`/?gm=<token>`): the whole GM side from a browser.
  * **Tabs along the top**, like the player's page: **Character** (a dropdown of every character, with its editable sheet; *+ New
    character* opens the **character creator**, see [Characters](#characters), and *Random* makes one at once), **Parties** (every party
    with its characters as a reduced sheet, name / class / kin, each linking to its sheet; adding, renaming, messaging and deleting
    parties), **Chat** (for now the player's own chat, of the character picked in the dropdown) and **Rules**. Each player's personal link is on their sheet's header (*Player link ↗*).
  * A **Reference** index, exactly the same one the players have:
    * Entries with tabs: Skills (*All skills* / *General info*), Gear (*General* / *Weapons* / *Armor*).
    * Rule tables drawn in place, and each category's Intro always open above its list.
    * **Creatures** with their picture next to the stat block, and **NPC / Animal / Monster** filter tags.
    * **World › NPC Creator**: *Create Random NPC* rolls a name, attitude, kin, motivation, profession and trait from the book's
      lists (`data/system/npcs.yaml`), and each result has its own *Re-roll*.
* **Player interface** (`/?t=<token>`): on their phone and with **their personal link**, each player sees their complete sheet,
  their party's status and the whole Reference (exactly what the GM sees there: rules, creatures, NPC Creator, everything). It updates
  by itself every few seconds, lets them edit their sheet and chat with the GM, and it shows no GM notes or other players' data.

The normal way is to **open `Skaldbok.bat`** and, when you want the players to join, press **Start server** in the app's **General Settings**
(with *Launch server when opening* it starts with the app), then **Open as GM**. Players on the same
Wi-Fi open their link (`http://<your-pc-ip>:8080/?t=<token>`, copied from that tab). The page is compiled once with Node
(`cd web/client && npm install && npm run build`; `Build.bat` does it for you). You can also run it by hand, without the app:

```
build\release\skaldbok_web.exe --public-url http://my-address
```

The links are also printed by `skaldbok_web --links`. Details, security, exposing it over HTTPS and the API: [`docs/WEB.md`](docs/WEB.md).

## Players edit their sheet

On the web, each player can **edit everything** on their character (except kin, profession and school, chosen when it is created by the GM): attributes, HP/WP and their maximums, conditions, skills (level, trained, advancement mark), abilities and spells, weapons, armor,
inventory, coins, texts. What changes syncs by itself, field by field, and you see it **live** even while looking at that sheet.

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
* **Import**: **General Settings › Content packs** › *Import a pack folder…* or *Import a .zip…* (or
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

Each character is a YAML file (`docs/CHARACTERS.md`) that the web server reads and writes. The GM web page brings a creator that follows
chapter 2 of the Rulebook, nine steps with the book's rolls:

1. kin (pick or roll D12) · 2. profession (pick or roll D10; the mage picks a school of magic) · 3. age (young / adult / old, pick or D6) ·
4. attributes (4D6 dropping the lowest, assigned as they are rolled, a final swap of two, or by hand) · 5. trained skills (6 from the
profession, the rest free; level = double the base chance) · 6. heroic ability or, for the mage, 3 spells and 3 cantrips · 7. starting
gear (one of the three sets with its dice rolled, or **Custom**: anything from the rules with the total cost added up) · 8. name, nickname,
weakness, memento and appearance (with the book's tables) · 9. review. **Random character** makes a whole one at once.

Everything you can pick comes from the loaded content (Core + active packs). Afterwards the sheet is edited freely on the web page:
attributes, HP/WP, conditions, skills, abilities, spells, gear, coins and notes. The load counts each item's weight (the book's, 1/4 for a
field ration) against the encumbrance limit. The sheet is laid out like the **first (green) page of the official character sheet**.

## Chat and broadcast

The web page has a real chat. Each character has their conversation with the GM (a player never sees another's nor can write to them), ordered
by activity and with unread counts, with pictures and a "seen" mark when the player has read it. A **broadcast** (to a party, from its page, or to
everyone) appears in each conversation as a highlighted banner (red border and "BROADCAST"), different from an ordinary message, and the
players' replies come back to you, one by one.

Each message is its own file (`chat/<character>/<id>.json`, pictures in `chat/media/`) and each side saves only how far it has read, so the GM
and the server can write at the same time without stepping on each other.

## How it is built

```
data/packs/core/    (the Core pack, the base: creatures, spells, kin, professions, gear… + images/)
data/system/        (the books' generic rules and tables, the World chapter, the NPC lists, the intro of each page)
                          ▼
   skaldbok  (C++ / SDL3 / Dear ImGui: packs and the server)  ◄── homebrew and house-rules packs (user folder)
   skaldbok_web  (C++ server + Vue 3 page)  ── the data, for the GM and the players in a browser
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
The user's files (settings, characters, parties, chat, imported packs) go to `%APPDATA%\skaldbok\gm\` (`--prefs <folder>` to change it).

## Lightweight

All the content is read once at startup from YAML and the desktop app does not redraw while nothing changes. With `--software` it uses SDL's
software renderer.

## Structure

```
                ── core (libgm_core): no UI and no server; every front-end uses it ──
src/parsing/    reads YAML and turns it into the internal model: content (packs), packs (import), jsonutil / yamlutil, jsondir
                (per-user stores that reread what another program writes), fsutil (files), fts/sql (search index)
src/game/       the game logic over that model: model (data), character, creation (rules), party, encounter, dice,
                messages (chat), sheet_edit (per-field editing and rule review), changelog, settings, web_link
                ── front-ends: each one links gm_core and nothing else; none includes another ──
src/ui/         the desktop app (ImGui), only the settings screen: main, app (the shell), settings_screen (content packs and the web
                server), packs_panel, host.h (what the screen needs from the shell), ui_common, fonts, textures, filedialog
src/web/        the web server: a socketless app (web_app), JSON views (web_views), the character creator's data (web_creation),
                tokens (web_access), HTTP (web_server)
web/client/     the web page for GM and players (Vue 3 + Vite); built with npm and served by skaldbok_web
tests/          windowless ctest: content and packs, characters and parties, encounter, chat, sheet editing (two writers on one
                file) and the web (authentication, privacy, real HTTP); the page's own tests are in web/client (npm test)
tools/          the PDF → SQLite converter + Core pack in Python, and pack_check
docs/           HOMEBREW.md (pack format), CHARACTERS.md (characters and parties), WEB.md (the web interfaces)
docs/examples/  frostmarch-tales: an example pack, documentation only (the app does not use it)
scripts/        build.ps1, package.ps1, deploy.ps1 (Windows)
Build.bat       builds the Vue client and the C++ release in one go
```

**Adding a front-end** (another API, another UI): a new target that links `gm_core`; everything it needs (reading packs, characters, rules) is
already there. Whatever has to be shared between front-ends goes into the core, never from one to another.

Command-line options of the desktop app: `--shot file.png` (capture and exit), `--import <pack>`, `--no-web` (do not start the server),
`--data <folder>`, `--prefs <folder>` (isolates settings, characters and packs), `--size 1200x800`, `--software`.

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
* The desktop app no longer has an encounter tracker, a master screen or homebrew forms: they were dropped with its data views. The web
  page does not have them yet.

## License

Free software under the **GNU General Public License v3.0 or later** (full text in [`LICENSE`](LICENSE)): you may use, study, modify and
redistribute it, and any modified version you distribute must also be free under the same license.

* Dependencies (downloaded at build time, `cmake/Dependencies.cmake`), all GPL-compatible: Dear ImGui, cpp-httplib, nlohmann/json,
  yaml-cpp, miniz, stb_image and Vue (MIT), SDL3 (zlib) and SQLite (public domain).
* **Dragonbane** is a trademark and work of Free League Publishing. This project is not affiliated with them. The original PDFs
  (`References/`) are **not** part of the repository. `data/` holds the content extracted from the books (rules, creatures, spells, tables and
  art), which belongs to Free League and is here for personal use; anyone who wants to redistribute it needs the rights to do so. Generating the
  data from your own legal copy of the books is described in `tools/README.md`.
* The homebrew content packs (`docs/examples/`) are original and are published under the same license.
