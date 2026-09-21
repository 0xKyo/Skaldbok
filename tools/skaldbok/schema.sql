-- Dragonbane database. All text is English, verbatim from the books.
-- Every record says which book it comes from (source_id) and carries the PHYSICAL pdf page (page),
-- the number printed on that page (printed_page) and pdf_link = "<file>#page=<n>" to open the original.
PRAGMA foreign_keys = ON;

CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE sources (
    id          INTEGER PRIMARY KEY,
    key         TEXT NOT NULL UNIQUE,            -- rulebook | bestiary | adventure
    title       TEXT NOT NULL,
    file        TEXT NOT NULL,                   -- path of the PDF relative to the project root
    page_offset INTEGER NOT NULL                 -- physical page - printed page
);

CREATE TABLE pages (
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    page         INTEGER NOT NULL,               -- physical PDF page, 1-based
    printed_page INTEGER,                        -- number printed on the page (NULL for covers/front matter)
    chapter      TEXT,                           -- running header of the page
    text         TEXT NOT NULL,                  -- whole page in reading order (fallback / audit copy)
    PRIMARY KEY (source_id, page)
);

-- Book structure taken from the PDF bookmarks.
CREATE TABLE sections (
    id            INTEGER PRIMARY KEY,
    source_id     INTEGER NOT NULL REFERENCES sources(id),
    parent_id     INTEGER REFERENCES sections(id),
    ord           INTEGER NOT NULL,              -- book order
    level         INTEGER NOT NULL,              -- 1 = chapter
    kind          TEXT NOT NULL,                 -- chapter|section|sidebar|table|optional_rule|ability|statblock|npc|monster|map
    title         TEXT NOT NULL,
    path          TEXT NOT NULL,                 -- "Chapter > Section > Subsection"
    chapter_no    INTEGER,
    page_start    INTEGER NOT NULL,
    page_end      INTEGER NOT NULL,
    printed_start INTEGER,
    pdf_link      TEXT NOT NULL,
    anchored      INTEGER NOT NULL,              -- 1 if the heading was located on the page
    body          TEXT NOT NULL                  -- the section's own text (children not included)
);
CREATE INDEX idx_sections_parent ON sections(parent_id);
CREATE INDEX idx_sections_kind   ON sections(kind);

-- Every table of the book, generic form.
CREATE TABLE game_tables (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    section_id   INTEGER REFERENCES sections(id),
    monster_id   INTEGER REFERENCES monsters(id),
    title        TEXT NOT NULL,
    dice         TEXT,                           -- D6, D20, ... NULL for plain tables
    columns      TEXT NOT NULL,                  -- JSON array of column names
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

CREATE TABLE game_table_rows (
    id         INTEGER PRIMARY KEY,
    table_id   INTEGER NOT NULL REFERENCES game_tables(id) ON DELETE CASCADE,
    ord        INTEGER NOT NULL,
    roll_min   INTEGER,
    roll_max   INTEGER,
    roll_text  TEXT,                             -- "1-2", "10" as printed
    cells      TEXT NOT NULL                     -- JSON array, one string per column
);
CREATE INDEX idx_rows_table ON game_table_rows(table_id, ord);

-- ---------------------------------------------------------------------------------------------
-- Monsters, creatures and NPCs from the Rulebook, the Bestiary and the Adventure (kept apart by source).
-- The same creature can exist in two books (Goblin, Orc...): they are separate rows, one per source.
CREATE TABLE monsters (
    id                INTEGER PRIMARY KEY,
    source_id         INTEGER NOT NULL REFERENCES sources(id),
    name              TEXT NOT NULL,
    kind              TEXT NOT NULL,             -- monster | npc | animal
    category          TEXT,                      -- Bestiary chapter ("Nightkin") or adventure chapter
    description       TEXT NOT NULL DEFAULT '',
    quote             TEXT,
    random_encounter  TEXT,
    adventure_seed    TEXT,
    stats_ref         TEXT,                      -- e.g. "The Lady is a ghost with stats as per page 87 in the Rulebook."
    attack_dice       TEXT,                      -- die of the Monster Attacks table: D6 (usual), D4...
    page              INTEGER NOT NULL,
    printed_page      INTEGER,
    pdf_link          TEXT NOT NULL,
    image             TEXT,                      -- path relative to the project root (data/packs/core/images/...)
    image_page        INTEGER,
    image_pdf_link    TEXT
);
CREATE INDEX idx_monsters_source ON monsters(source_id, name);

-- One monster can have several stat blocks (Goblin: Scout, Warrior).
CREATE TABLE monster_statblocks (
    id            INTEGER PRIMARY KEY,
    monster_id    INTEGER NOT NULL REFERENCES monsters(id) ON DELETE CASCADE,
    ord           INTEGER NOT NULL,
    variant       TEXT,                          -- "Scout", NULL when the monster has a single block
    ferocity      TEXT,
    size          TEXT,
    movement      TEXT,
    armor         TEXT,
    hp            TEXT,
    wp            TEXT,
    damage_bonus  TEXT,
    skills        TEXT,
    weapons       TEXT,
    fields        TEXT NOT NULL,                 -- JSON object: EVERY labelled field of the block, as printed
    page          INTEGER NOT NULL,
    printed_page  INTEGER,
    pdf_link      TEXT NOT NULL
);

-- The "Monster Attacks" D6 tables: the thing the GM rolls on during combat.
CREATE TABLE monster_attacks (
    id            INTEGER PRIMARY KEY,
    monster_id    INTEGER NOT NULL REFERENCES monsters(id) ON DELETE CASCADE,
    ord           INTEGER NOT NULL,
    roll_min      INTEGER NOT NULL,
    roll_max      INTEGER NOT NULL,
    roll_text     TEXT NOT NULL,                 -- "1", "1-2" as printed
    name          TEXT,                          -- "Horse Kick!"
    text          TEXT NOT NULL,                 -- full text including the name
    page          INTEGER NOT NULL,
    printed_page  INTEGER,
    pdf_link      TEXT NOT NULL
);
CREATE INDEX idx_attacks_monster ON monster_attacks(monster_id, ord);

-- Named special rules of a monster ("Nocturnal", "Non-Monster", "PC Ability: Resilient"...).
CREATE TABLE monster_abilities (
    id            INTEGER PRIMARY KEY,
    monster_id    INTEGER NOT NULL REFERENCES monsters(id) ON DELETE CASCADE,
    ord           INTEGER NOT NULL,
    name          TEXT NOT NULL,
    kind          TEXT NOT NULL,                 -- ability | pc_ability
    text          TEXT NOT NULL,
    page          INTEGER NOT NULL,
    printed_page  INTEGER,
    pdf_link      TEXT NOT NULL
);

-- ---------------------------------------------------------------------------------------------
-- Rulebook rules content, typed. Each row points at its section (full text) and its page.

CREATE TABLE skills (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    section_id   INTEGER REFERENCES sections(id),
    name         TEXT NOT NULL,
    attribute    TEXT,                           -- STR CON AGL INT WIL CHA
    category     TEXT NOT NULL,                  -- core | weapon | magic
    description  TEXT NOT NULL,
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

CREATE TABLE abilities (                         -- heroic abilities and kin innate abilities
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    section_id   INTEGER REFERENCES sections(id),
    name         TEXT NOT NULL,
    type         TEXT NOT NULL,                  -- heroic | kin
    kin          TEXT,                           -- for kin abilities
    requirement  TEXT,
    wp_cost      TEXT,                           -- "3", "—" as printed
    description  TEXT NOT NULL,
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

CREATE TABLE kin (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    section_id   INTEGER REFERENCES sections(id),
    name         TEXT NOT NULL,
    description  TEXT NOT NULL,
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

CREATE TABLE professions (
    id             INTEGER PRIMARY KEY,
    source_id      INTEGER NOT NULL REFERENCES sources(id),
    section_id     INTEGER REFERENCES sections(id),
    name           TEXT NOT NULL,
    description    TEXT NOT NULL,
    key_attribute  TEXT,
    skills         TEXT,                         -- as printed: "Axes, Brawling, ..."
    heroic_ability TEXT,                         -- as printed
    page           INTEGER NOT NULL,
    printed_page   INTEGER,
    pdf_link       TEXT NOT NULL
);

CREATE TABLE spells (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    section_id   INTEGER REFERENCES sections(id),
    name         TEXT NOT NULL,
    school       TEXT NOT NULL,                  -- General Magic | Animism | Elementalism | Mentalism
    is_trick     INTEGER NOT NULL,               -- 1 = magic trick
    rank         TEXT,
    prerequisite TEXT,
    requirement  TEXT,
    casting_time TEXT,
    range        TEXT,
    duration     TEXT,
    description  TEXT NOT NULL,
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

CREATE TABLE conditions (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    name         TEXT NOT NULL,
    attribute    TEXT NOT NULL,                  -- the attribute (and its skills) that get a bane
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

-- Equipment, as printed (cost/supply/etc. stay text: "5 silver", "2 gold", "Uncommon").
CREATE TABLE weapons (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    table_id     INTEGER REFERENCES game_tables(id),
    kind         TEXT NOT NULL,                  -- melee | ranged
    name         TEXT NOT NULL,
    grip         TEXT, str_req TEXT, range TEXT, damage TEXT, durability TEXT,
    cost         TEXT, supply TEXT, features TEXT,
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

CREATE TABLE armor (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    table_id     INTEGER REFERENCES game_tables(id),
    name         TEXT NOT NULL,
    armor_rating TEXT, cost TEXT, supply TEXT, effect TEXT,
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

CREATE TABLE gear_items (
    id           INTEGER PRIMARY KEY,
    source_id    INTEGER NOT NULL REFERENCES sources(id),
    table_id     INTEGER REFERENCES game_tables(id),
    category     TEXT NOT NULL,                  -- the table it comes from (Tools, Medicine, Services...)
    name         TEXT NOT NULL,
    cost         TEXT, supply TEXT, weight TEXT,
    effect       TEXT,                           -- "Effect" / "Comment" column
    page         INTEGER NOT NULL,
    printed_page INTEGER,
    pdf_link     TEXT NOT NULL
);

-- Full-text search across everything.
CREATE VIRTUAL TABLE search_index USING fts5(
    kind, ref_id UNINDEXED, title, body, source UNINDEXED, page UNINDEXED,
    tokenize = 'porter unicode61 remove_diacritics 2'
);
