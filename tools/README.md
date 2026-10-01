# Dragonbane data extraction (how the data was first made)

> **The app no longer reads a database.** Its data is one content pack, `data/packs/core` (creatures, spells, kin...) plus `data/system/` (the books'
> rules and tables, the adventure, and the intro of each page), loaded like any homebrew pack — see
> [`docs/HOMEBREW.md`](../docs/HOMEBREW.md). These scripts are the original PDF extraction: they wrote an intermediate
> `data/skaldbok.db` and part of the Core pack from it. The rules and tables (`data/system/rules.json`) were taken from that database once and are kept as JSON from then on,
> so nothing here is needed to run the app (and the scripts do not write `rules.json`). **Do not run `build_db.py` / `export_packs.py` on
> an existing `data/`**: they regenerate `data/packs/core/` (with the old header in `rules.json`), overwriting what is edited by hand now.

One-off converter: reads the three PDFs in `References/` and writes

* `data/skaldbok.db` (SQLite): the books themselves — page text, headings, every table, full-text index;
* `data/packs/core/`: the **Core content pack** (JSON + `images/` with the creature art).

Nothing here ships with the app.

```
pip install -r tools/requirements.txt
python tools/build_db.py          # rebuilds data/skaldbok.db, data/packs/core/ (via export_packs.py) and its images
python tools/validate.py          # sanity report (exit code 1 on problems)
python tools/export_packs.py      # only regenerates data/packs/core/ from the existing database
```

## What is in the database

Every record carries `source_id` (rulebook / bestiary / adventure), the physical `page`, the `printed_page`
and `pdf_link` (`References/<file>.pdf#page=N`) to open the original.

| Table | Content |
|---|---|
| `sources`, `pages`, `sections` | Book text in reading order, structured by the PDF bookmarks (the app takes the rules text and tables from `data/system/rules.json` now) |
| `game_tables`, `game_table_rows` | Every table (dice tables validated to cover 1..N; plain tables by column) |
| `monsters`, `monster_statblocks`, `monster_attacks`, `monster_abilities` | Creatures/NPCs of all three books, kept apart by source; attack tables row by row; `image` = art |
| `abilities`, `kin`, `professions`, `skills`, `spells`, `conditions` | Rulebook rules, typed |
| `weapons`, `armor`, `gear_items` | Rulebook equipment, typed |
| `search_index` | FTS5 over everything (`SELECT * FROM search_index WHERE search_index MATCH 'centaur'`) |

The app reads none of it any more: rules and tables are in `data/system/rules.json`, and creatures, spells, abilities, skills, kin,
professions and equipment in the Core pack (exported from the typed tables above by `export_packs.py`).

```sql
-- the attacks of the Centaur from the Bestiary, with a link to the original page
SELECT a.roll_text, a.text, a.pdf_link
FROM monster_attacks a JOIN monsters m ON m.id = a.monster_id JOIN sources s ON s.id = m.source_id
WHERE m.name = 'Centaur' AND s.key = 'bestiary' ORDER BY a.ord;
```

## The Core pack

`export_packs.py` writes `data/packs/core/{manifest,creatures,spells,abilities,skills,kin,professions,weapons,armor,gear,tables}.json`.
Beyond copying the typed tables it adds what the character creator needs and the books only print in running text or tables:

* kin: `movement` (Movement table, p.29), `names` (the six first names), `innate_abilities`;
* professions: `starting_gear` (the three printed sets), `nicknames`, `heroic_abilities` (split at commas / "or"), and for the mage
  `skills_by_school` and `magic` (3 spells + 3 tricks of rank 1);
* skills: the **weapon skills' attributes** (Axes STR, Bows AGL, Brawling STR, Crossbows AGL, Hammers STR, Knives AGL, Slings AGL,
  Spears STR, Staves AGL, Swords STR). They are not in the running text; they are printed on the Rulebook's character sheet
  (page 127) and live in `WEAPON_SKILL_ATTRIBUTE`;
* `tables.json`: the Weakness, Memento and Appearance tables with a `role`, so the creator can roll on them;
* creature ids are `<book>-<name>` (`bestiary-goblin`), unique per book, so the same creature in two books never collides.

## Notes on reliability

* Text is verbatim. Line-break hyphens are healed only when the joined word exists elsewhere in the book.
* Nine chapter-opening drop caps are images without a text layer; their letters (W T A L S T T T T) were read
  off the rendered pages and live in `pdfio.RULEBOOK_DROP_CAPS`.
* Page references *inside* the text ("see page 87") are **printed** page numbers, as in the books.
* `build_db.py` prints warnings for anything it cannot confirm against the books' own indexes.

## The Misty Vale adventure (`mistyvale.py`)

```
python tools/mistyvale.py        # writes data/packs/mistyvale/ (safe to rerun: it only writes that folder)
```

Reads `References/MistyValeAdventure.pdf` with the same library as the rest (`tools/skaldbok`) and writes a pack the app can validate
(`build/release/pack_check data/packs/mistyvale --data data`):

* `manifest.yaml`: the pack and the PDF it comes from (so a `page` can open the original);
* `adventure.yaml`: the book as a tree of `sections` (chapter, section, `location` with its number, `npc`, `monster`, `sidebar`, `map`,
  `table`, `text`). Every node has `id`, `title`, `kind`, the physical `page`, and a `body` when it has text. NPCs and monsters have
  **no stat block** here: they point at the creature a pack already has: the named NPCs are in the pack's own `creatures.yaml` (`creatures: [mistyvale/monster/hardy]`), the groups and monsters in Core. A `table` node
  names its table in `tables.yaml` (`table:`). A `text` node titled *Continued* is the place's own text that the book prints after a
  creature's block (the exits, other notes); it sits right after that creature;
* `tables.yaml`: the adventure's tables (random events, rumors, random encounters, demonic omens...). The attack tables of creatures
  are not here: they are in Core with the creature.

Each node can carry `keywords:` (the words of the text that become links to it): the titles of chapters and places, the names of the
people, and the proper names the book repeats at least `MIN_REPEATS` times that name something with a page. Where a word goes when the
title does not say it (*Um-Durman*, *Azrahel Koth*, *Eledain*...) is chosen in `KEYWORD_TARGETS`; the script prints the frequent names
that no page takes (*Sathmog*, *Kummer Mountains*, *Mirror Lake*...).

**Pictures.** Every picture of the PDF that is art is taken out (needs `pillow` too) and given to a node as `images:` (paths inside the
pack): `images/chapters/` the strip of the map that opens a chapter, `images/maps/` the plan of a place (a `map` node; the big map of the
valley, printed over two pages, has both halves), `images/people/` the portrait of an NPC or monster (found by the name printed over it),
`images/art/` the rest (an illustration goes to the heading nearest to it). Left out: the blank parchment boxes the text is printed on and
the page furniture (pictures used on many pages); the creature art Core had already taken stays where it is. The named NPCs of the pack get
their portrait as `image:` in `creatures.yaml` too. About 12 MB: maps are flattened on the cream of the app (JPEG) and portraits keep
their soft edges (PNG or JPEG, small).

Things worth knowing: the 15 chapter-opening drop caps are pictures (their letters are in `DROP_CAPS`); `dice` of a table that the extractor could not read is taken from its last roll (the Fort Malus
table has no dice because it stops at 7). Creatures printed inside a sidebar whose name is not in Core (the viper of the Dead Eyes
Cave) keep their stat line in the text.
