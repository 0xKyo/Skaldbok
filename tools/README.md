# Dragonbane data extraction

One-off converter: reads the three PDFs in `References/` and writes

* `data/dragonbane.db` (SQLite): the books themselves — page text, headings, every table, full-text index;
* `data/packs/core/`: the **Core content pack** (JSON + `images/` with the creature art) that the app loads like any
  homebrew pack — see [`docs/HOMEBREW.md`](../docs/HOMEBREW.md).

The C++ app only reads those; nothing here ships with it.

```
pip install -r tools/requirements.txt
python tools/build_db.py          # rebuilds data/dragonbane.db, data/packs/core/ (via export_packs.py) and its images
python tools/validate.py          # sanity report (exit code 1 on problems)
python tools/export_packs.py      # only regenerates data/packs/core/ from the existing database
python tools/render_pages.py      # optional: page images for the app's built-in original-page viewer (~65 MB)
```

## What is in the database

Every record carries `source_id` (rulebook / bestiary / adventure), the physical `page`, the `printed_page`
and `pdf_link` (`References/<file>.pdf#page=N`) to open the original.

| Table | Content |
|---|---|
| `sources`, `pages`, `sections` | Book text in reading order, structured by the PDF bookmarks (kept in the database; the app no longer shows it: only tables, pages and the search over tables use it) |
| `game_tables`, `game_table_rows` | Every table (dice tables validated to cover 1..N; plain tables by column) |
| `monsters`, `monster_statblocks`, `monster_attacks`, `monster_abilities` | Creatures/NPCs of all three books, kept apart by source; attack tables row by row; `image` = art |
| `abilities`, `kin`, `professions`, `skills`, `spells`, `conditions` | Rulebook rules, typed |
| `weapons`, `armor`, `gear_items` | Rulebook equipment, typed |
| `search_index` | FTS5 over everything (`SELECT * FROM search_index WHERE search_index MATCH 'centaur'`) |

The app reads **book text, tables and pages** from the database; creatures, spells, abilities, skills, kin, professions and
equipment come from the Core pack, exported from the typed tables above by `export_packs.py`.

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
